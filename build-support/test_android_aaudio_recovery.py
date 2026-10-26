"""Compile the actual Android AAudio backend against deterministic host seams."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


STUBS = r'''
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>
using u8=uint8_t; using u32=uint32_t; using u64=uint64_t;
#define pxAssert assert
#define ANDROID_LOG_INFO 1
#define ANDROID_LOG_WARN 2
#define ANDROID_LOG_ERROR 3
#define EMUCOREX_DIAGNOSTIC_LOG_PRINT(...) (void)0
#define EMUCOREX_DIAGNOSTIC_LOG_WRITE(...) (void)0
inline int __android_log_print(int,const char*,const char*,...) { return 0; }
class Error { public: template<class... T> static void SetStringFmt(Error* e,const char* text,T...) { if(e)e->text=text; }
 const std::string& GetDescription() const { return text; } private: std::string text; };
enum class AudioExpansionMode { Disabled,StereoLFE,Quadraphonic,QuadraphonicLFE,Surround51,Surround71,Count };
struct AudioStreamParameters { AudioExpansionMode expansion_mode=AudioExpansionMode::Disabled; u32 output_latency_ms=20,buffer_ms=50; bool minimal_output_latency=true; };
inline std::atomic<int> mixer_initializations{0}, consumed_frames{0};
class AudioStream {
public:
 using SampleType=float;
 enum ReadChannel { READ_CHANNEL_FRONT_LEFT,READ_CHANNEL_FRONT_RIGHT,READ_CHANNEL_FRONT_CENTER,READ_CHANNEL_LFE,READ_CHANNEL_REAR_LEFT,READ_CHANNEL_REAR_RIGHT,READ_CHANNEL_SIDE_LEFT,READ_CHANNEL_SIDE_RIGHT };
 using SampleReader=void(*)(float*,const float*,u32);
 AudioStream(u32 rate,const AudioStreamParameters& parameters):m_sample_rate(rate),m_parameters(parameters){}
 virtual ~AudioStream()=default;
 virtual void SetPaused(bool paused) {m_paused=paused;}
 bool IsPaused() const {return m_paused;}
 static u32 GetBufferSizeForMS(u32 rate,u32 ms){return rate*ms/1000;}
 static std::unique_ptr<AudioStream> CreateSDLAudioStream(u32,const AudioStreamParameters&,bool,Error*);
protected:
 void BaseInitialize(SampleReader,bool){++mixer_initializations;}
 void ReadFrames(float* out,u32 frames){std::fill_n(out,frames*m_output_channels,1.0f);consumed_frames+=frames;}
 static void StereoSampleReaderImpl(float*,const float*,u32){}
 template<AudioExpansionMode,ReadChannel...> static void SampleReaderImpl(float*,const float*,u32){}
 u32 m_sample_rate; AudioStreamParameters m_parameters; u8 m_output_channels=2; bool m_paused=false;
};
using aaudio_result_t=int; using aaudio_stream_state_t=int; using aaudio_data_callback_result_t=int;
constexpr int AAUDIO_OK=0,AAUDIO_ERROR_DISCONNECTED=-899,AAUDIO_ERROR_INTERNAL=-896,AAUDIO_ERROR_UNAVAILABLE=-889;
constexpr int AAUDIO_DIRECTION_OUTPUT=0,AAUDIO_SHARING_MODE_SHARED=1,AAUDIO_PERFORMANCE_MODE_LOW_LATENCY=1,AAUDIO_FORMAT_PCM_FLOAT=2,AAUDIO_CALLBACK_RESULT_CONTINUE=0;
constexpr int AAUDIO_STREAM_STATE_OPEN=1,AAUDIO_STREAM_STATE_STARTED=2,AAUDIO_STREAM_STATE_STARTING=3,AAUDIO_STREAM_STATE_PAUSING=4,AAUDIO_STREAM_STATE_PAUSED=5,AAUDIO_STREAM_STATE_DISCONNECTED=6,AAUDIO_STREAM_STATE_CLOSING=7,AAUDIO_STREAM_STATE_CLOSED=8;
struct AAudioStream;
using Data=aaudio_data_callback_result_t(*)(AAudioStream*,void*,void*,int32_t);
using Failure=void(*)(AAudioStream*,void*,aaudio_result_t);
struct AAudioStreamBuilder { Data data=nullptr; Failure error=nullptr; void* userdata=nullptr; };
struct AAudioStream { std::atomic<int> state{AAUDIO_STREAM_STATE_OPEN}; Data data; Failure error; void* userdata; std::atomic<bool> closed{false}; std::atomic<int> callbacks{0}; };
aaudio_result_t AAudio_createStreamBuilder(AAudioStreamBuilder**);
void AAudioStreamBuilder_setDirection(AAudioStreamBuilder*,int);
void AAudioStreamBuilder_setSharingMode(AAudioStreamBuilder*,int);
void AAudioStreamBuilder_setPerformanceMode(AAudioStreamBuilder*,int);
void AAudioStreamBuilder_setSampleRate(AAudioStreamBuilder*,int32_t);
void AAudioStreamBuilder_setChannelCount(AAudioStreamBuilder*,int32_t);
void AAudioStreamBuilder_setFormat(AAudioStreamBuilder*,int);
void AAudioStreamBuilder_setBufferCapacityInFrames(AAudioStreamBuilder*,int32_t);
void AAudioStreamBuilder_setDataCallback(AAudioStreamBuilder*,Data,void*);
void AAudioStreamBuilder_setErrorCallback(AAudioStreamBuilder*,Failure,void*);
aaudio_result_t AAudioStreamBuilder_openStream(AAudioStreamBuilder*,AAudioStream**);
void AAudioStreamBuilder_delete(AAudioStreamBuilder*);
int32_t AAudioStream_getFramesPerBurst(AAudioStream*);
aaudio_result_t AAudioStream_setBufferSizeInFrames(AAudioStream*,int32_t);
int32_t AAudioStream_getBufferSizeInFrames(AAudioStream*);
int32_t AAudioStream_getBufferCapacityInFrames(AAudioStream*);
aaudio_result_t AAudioStream_requestStart(AAudioStream*);
aaudio_result_t AAudioStream_requestPause(AAudioStream*);
aaudio_result_t AAudioStream_requestStop(AAudioStream*);
aaudio_result_t AAudioStream_close(AAudioStream*);
aaudio_stream_state_t AAudioStream_getState(AAudioStream*);
aaudio_result_t AAudioStream_waitForStateChange(AAudioStream*,int,int*,int64_t);
const char* AAudio_convertResultToText(int);
'''

HARNESS = r'''
#include "stubs.h"
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include "backend.inc"
using namespace std::chrono_literals;
std::mutex fake_mutex;
std::vector<std::unique_ptr<AAudioStream>> streams;
std::atomic<AAudioStream*> current{nullptr};
std::atomic<int> open_attempts{0},fail_opens{0},fail_builders{0},fail_starts{0},callback_violations{0};
std::atomic<bool> error_during_close{false},error_during_start{false};
thread_local bool in_error=false;

bool invoke_error(AAudioStream* s,int error=AAUDIO_ERROR_DISCONNECTED) {
 {std::lock_guard lock(fake_mutex);if(s->closed)return false;++s->callbacks;}
 s->state=AAUDIO_STREAM_STATE_DISCONNECTED;in_error=true;s->error(s,s->userdata,error);in_error=false;--s->callbacks;return true;
}
bool audible(AAudioStream* s) {
 {std::lock_guard lock(fake_mutex);if(s->closed||s->state!=AAUDIO_STREAM_STATE_STARTED)return false;++s->callbacks;}
 float samples[128]{};s->data(s,s->userdata,samples,64);--s->callbacks;return samples[0]==1.0f;
}
bool consume_failure(std::atomic<int>& counter){int value=counter;while(value>0){if(counter.compare_exchange_weak(value,value-1))return true;}return false;}
aaudio_result_t AAudio_createStreamBuilder(AAudioStreamBuilder** b){if(consume_failure(fail_builders)){*b=nullptr;return AAUDIO_ERROR_INTERNAL;}*b=new AAudioStreamBuilder;return AAUDIO_OK;}
#define SETTER(name) void name(AAudioStreamBuilder*,int){}
SETTER(AAudioStreamBuilder_setDirection) SETTER(AAudioStreamBuilder_setSharingMode)
SETTER(AAudioStreamBuilder_setPerformanceMode) SETTER(AAudioStreamBuilder_setSampleRate)
SETTER(AAudioStreamBuilder_setChannelCount) SETTER(AAudioStreamBuilder_setFormat)
SETTER(AAudioStreamBuilder_setBufferCapacityInFrames)
void AAudioStreamBuilder_setDataCallback(AAudioStreamBuilder* b,Data d,void* u){b->data=d;b->userdata=u;}
void AAudioStreamBuilder_setErrorCallback(AAudioStreamBuilder* b,Failure e,void* u){b->error=e;b->userdata=u;}
aaudio_result_t AAudioStreamBuilder_openStream(AAudioStreamBuilder* b,AAudioStream** out){
 if(in_error)++callback_violations;++open_attempts;
 int failures=fail_opens.load();while(failures>0){if(fail_opens.compare_exchange_weak(failures,failures-1)){*out=nullptr;return AAUDIO_ERROR_UNAVAILABLE;}}
 auto stream=std::make_unique<AAudioStream>();stream->data=b->data;stream->error=b->error;stream->userdata=b->userdata;*out=stream.get();
 {std::lock_guard lock(fake_mutex);streams.push_back(std::move(stream));} current=*out;return AAUDIO_OK;
}
void AAudioStreamBuilder_delete(AAudioStreamBuilder* b){delete b;}
int32_t AAudioStream_getFramesPerBurst(AAudioStream*){return 192;}
aaudio_result_t AAudioStream_setBufferSizeInFrames(AAudioStream*,int32_t n){return n;}
int32_t AAudioStream_getBufferSizeInFrames(AAudioStream*){return 384;}
int32_t AAudioStream_getBufferCapacityInFrames(AAudioStream*){return 768;}
aaudio_result_t AAudioStream_requestStart(AAudioStream* s){if(consume_failure(fail_starts))return AAUDIO_ERROR_INTERNAL;s->state=AAUDIO_STREAM_STATE_STARTED;if(error_during_start.exchange(false))invoke_error(s);return AAUDIO_OK;}
aaudio_result_t AAudioStream_requestPause(AAudioStream* s){if(in_error)++callback_violations;s->state=AAUDIO_STREAM_STATE_PAUSED;return AAUDIO_OK;}
aaudio_result_t AAudioStream_requestStop(AAudioStream* s){if(in_error)++callback_violations;return AAUDIO_OK;}
aaudio_result_t AAudioStream_close(AAudioStream* s){
 if(in_error)++callback_violations;
 if(error_during_close.exchange(false))invoke_error(s);
 {std::lock_guard lock(fake_mutex);s->closed=true;s->state=AAUDIO_STREAM_STATE_CLOSED;}
 while(s->callbacks.load())std::this_thread::yield();return AAUDIO_OK;
}
aaudio_stream_state_t AAudioStream_getState(AAudioStream* s){return s->state;}
aaudio_result_t AAudioStream_waitForStateChange(AAudioStream* s,int,int* next,int64_t){*next=s->state;return AAUDIO_OK;}
const char* AAudio_convertResultToText(int){return "fake audio result";}
template<class Predicate> bool wait(Predicate predicate){auto until=std::chrono::steady_clock::now()+3s;while(std::chrono::steady_clock::now()<until){if(predicate())return true;std::this_thread::sleep_for(2ms);}return false;}
int main(){
 unsigned checks=0,failures=0;
 auto check=[&](bool value,const char* text){++checks;if(!value){++failures;std::printf("FAIL %s\n",text);}};
 Error error;auto owner=AudioStream::CreateSDLAudioStream(48000,{},false,&error);
 check(owner&&audible(current),"initial stream consumes mixer output");
 auto first=current.load();invoke_error(first);
 bool recovered=wait([&]{return current.load()!=first&&audible(current);});
 check(recovered,"route disconnect recovers without pause/resume");
 // Stop after the focused red case; the remaining checks exercise the fix.
 if(!recovered)return 1;
 check(first->closed,"disconnected stream closes off callback thread");
 check(mixer_initializations==1,"recovery preserves the initialized SPSC mixer");
 owner->SetPaused(true);auto paused=current.load();int attempts=open_attempts;invoke_error(paused);
 std::this_thread::sleep_for(350ms);
 check(open_attempts==attempts&&!audible(paused)&&owner->IsPaused(),"paused route change stays paused");
 owner->SetPaused(false);check(wait([&]{return current.load()!=paused&&audible(current);}),"resume recovers paused disconnected device");
 fail_opens=2;auto retry=current.load();attempts=open_attempts;invoke_error(retry);
 check(wait([&]{return open_attempts>attempts;}),"failed open attempt is observed");
 int retry_attempts=open_attempts;std::this_thread::sleep_for(50ms);
 check(open_attempts==retry_attempts,"failed recovery waits rather than spinning");
 check(wait([&]{return open_attempts>=attempts+3&&current.load()!=retry&&audible(current);}),"open failures retry automatically");
 fail_builders=1;auto builder_failure=current.load();invoke_error(builder_failure,AAUDIO_ERROR_INTERNAL);
 check(wait([&]{return current.load()!=builder_failure&&audible(current);}),"builder creation failure retries after non-disconnect error");
 fail_starts=1;auto start_failure=current.load();attempts=open_attempts;invoke_error(start_failure);
 check(wait([&]{return open_attempts>=attempts+2&&current.load()!=start_failure&&audible(current);}),"failed replacement start closes and retries");
 error_during_close=true;error_during_start=true;auto raced=current.load();attempts=open_attempts;invoke_error(raced);
 check(wait([&]{return open_attempts>=attempts+2&&audible(current);}),"stale closing error ignored and error during requestStart retained");
 for(int i=0;i<30;++i){invoke_error(current);std::this_thread::sleep_for(1ms);}
 check(wait([&]{return audible(current);}),"rapid route changes settle to audible stream");
 std::thread pauser([&]{for(int i=0;i<30;++i){owner->SetPaused(true);owner->SetPaused(false);}});
 for(int i=0;i<20;++i){invoke_error(current);std::this_thread::sleep_for(1ms);}pauser.join();
 check(wait([&]{return audible(current);}),"pause and route changes serialize safely");
 fail_opens=1000;attempts=open_attempts;invoke_error(current);wait([&]{return open_attempts>attempts;});
 auto before=std::chrono::steady_clock::now();owner.reset();
 check(std::chrono::steady_clock::now()-before<500ms,"shutdown interrupts retry and joins owned worker");
 attempts=open_attempts;std::this_thread::sleep_for(300ms);check(open_attempts==attempts,"no recovery after destruction");
 check(callback_violations==0,"no open/stop/pause/close inside error callback");
 {std::lock_guard lock(fake_mutex);for(auto& stream:streams)check(stream->closed,"all opened streams closed on shutdown");}
 std::printf("AAudio production recovery: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
'''


def emit_harness(directory):
    directory = Path(directory).resolve()
    directory.mkdir(parents=True, exist_ok=False)
    repo = Path(__file__).resolve().parents[1]
    production = repo / 'app/src/main/cpp/ARM_ANDROID/src/platform/android_aaudio_stream.cpp'
    (directory / 'stubs.h').write_text(STUBS, encoding='utf-8')
    for name in ('Host/AudioStream.h', 'common/AndroidLogging.h', 'common/Assertions.h', 'common/Error.h', 'aaudio/AAudio.h', 'android/log.h'):
        header = directory / name
        header.parent.mkdir(parents=True, exist_ok=True)
        header.write_text('#include "stubs.h"\n', encoding='utf-8')
    shutil.copyfile(production, directory / 'backend.inc')
    (directory / 'test.cpp').write_text(HARNESS, encoding='utf-8')
    (directory / 'README.md').write_text('''# AAudio recovery fault-injection executable

This compiles the saved production backend against fake AAudio and mixer seams.
It tests lifecycle, retries, pause and callback races on the target runtime. It
does not validate a real headset route or export a hook from the application.

PowerShell, with the emitted directory as the current directory:

```powershell
$audioTestToolchain = "$env:LOCALAPPDATA/Android/Sdk/ndk/29.0.14206865/toolchains/llvm/prebuilt/windows-x86_64"
& "$audioTestToolchain/bin/clang++.exe" --target=aarch64-linux-android29 -std=c++20 -O2 -pthread -static-libstdc++ -I . test.cpp -o aaudio-recovery-test
if ($LASTEXITCODE -ne 0) { throw 'Native recovery test compile failed' }
# Run only on the explicitly selected test device when other benchmarks finish.
adb -s SERIAL push aaudio-recovery-test /data/local/tmp/aaudio-recovery-test
adb -s SERIAL shell chmod 755 /data/local/tmp/aaudio-recovery-test
adb -s SERIAL shell /data/local/tmp/aaudio-recovery-test
```

`metadata.json` hashes the immutable emitted source snapshot. The static C++
runtime avoids requiring libc++_shared.so on the device. Retain compiler output,
executable SHA256 and device result alongside these sources.
''', encoding='utf-8')
    files = {path.relative_to(directory).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
             for path in sorted(directory.rglob('*')) if path.is_file()}
    (directory / 'metadata.json').write_text(json.dumps(dict(schema=1,
        production_source=str(production), production_sha256=files['backend.inc'],
        files_sha256=files), indent=2) + '\n', encoding='utf-8')
    return directory


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default=shutil.which('g++') or shutil.which('clang++'))
    parser.add_argument('--export-directory', type=Path,
                        help='Emit a new immutable harness directory and commands; do not compile or run')
    args = parser.parse_args()
    if args.export_directory is not None:
        print(emit_harness(args.export_directory))
        return 0
    if not args.compiler:
        parser.error('pass a C++20 host compiler with --compiler')
    with tempfile.TemporaryDirectory(prefix='aaudio-recovery-') as folder:
        temporary = Path(folder)
        source = emit_harness(temporary / 'source')
        binary = temporary / ('test.exe' if os.name == 'nt' else 'test')
        environment = os.environ.copy()
        environment['PATH'] = str(Path(args.compiler).resolve().parent) + os.pathsep + environment['PATH']
        subprocess.run([args.compiler, '-std=c++20', '-O2', '-pthread', '-I', str(source), str(source / 'test.cpp'), '-o', str(binary)], check=True, env=environment)
        return subprocess.run([str(binary)], env=environment, timeout=30).returncode


if __name__ == '__main__':
    raise SystemExit(main())
