// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Include at the end of TestFramebufferFetchPixelProof. Full production helper
// bodies, with real shader selector bits where available; the old baseline has
// no selector bits, so compiler macro inputs prove the shader consumer is absent.
{
    GLuint cb=0;glGenBuffers(1,&cb);GLint previous=0;glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING,0,&previous);
    const ScopedGuard freeCB([&](){glBindBufferBase(GL_UNIFORM_BUFFER,0,previous);glDeleteBuffers(1,&cb);});
    std::array<u8,1024> bytes{};
    const u32 keep=~128u,value=0;
    std::memcpy(bytes.data()+offsetof(GSHWDrawConfig::PSConstantBuffer,LineCovScale)+4,&keep,4);
    std::memcpy(bytes.data()+offsetof(GSHWDrawConfig::PSConstantBuffer,LineCovScale)+8,&value,4);
    glBindBuffer(GL_UNIFORM_BUFFER,cb);glBufferData(GL_UNIFORM_BUFFER,bytes.size(),bytes.data(),GL_STATIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER,0,cb);attach(rt.GetID(),0);
    const auto alphaSource=[&]<typename Selector>(Selector ps,bool substitute)
    {
        if constexpr(requires{ps.quantize_color;ps.substitute_alpha;})
        {
            ps.quantize_color=true;ps.substitute_alpha=substitute;
            return target.GetPSSource(ps,GLFramebufferFetchPlan{});
        }
        else
        {
            std::string text=target.GetPSSource(ps,GLFramebufferFetchPlan{});
            const size_t at=text.size()-target.m_shader_tfx_fs.size();
            text.insert(at,std::string("#define PS_QUANTIZE_COLOR 1\n#define PS_SUBSTITUTE_ALPHA ")+(substitute?"1\n":"0\n"));
            return text;
        }
    };
    bool quantize=true,substitution=true;
    for(bool sub:{false,true})
    {
        PSSelector ps{};ps.tfx=4;ps.no_color1=true;
        const std::string text=GLFetchOracle::ProbeMain(alphaSource(ps,sub),
            "\nuniform highp vec4 InputColor;void main(){vec4 C=InputColor;ps_fbmask(C);o_col0=C/255.;}\n");
        const GLuint fs=GLFetchOracle::Compile(GL_FRAGMENT_SHADER,text);const GLuint p=GLFetchOracle::Link(vertex,fs);if(fs)glDeleteShader(fs);
        if(!p){quantize=substitution=false;continue;}const ScopedGuard freeP([&](){glDeleteProgram(p);});
        glUseProgram(p);glUniform4f(glGetUniformLocation(p,"InputColor"),127.75f,63.5f,1.75f,200.75f);
        glDrawArrays(GL_TRIANGLES,0,3);std::array<u8,64> image{};glReadPixels(0,0,4,4,GL_RGBA,GL_UNSIGNED_BYTE,image.data());
        const std::array<u8,4> expected={127,63,1,static_cast<u8>(sub?72:200)};
        bool exact=glGetError()==GL_NO_ERROR;for(size_t i=0;i<image.size();i++)exact&=image[i]==expected[i%4];
        if(sub)substitution&=exact;else quantize&=exact;
        if(!exact)std::fprintf(stderr,"GL_EXACT_ALPHA sub=%u rgba=%u/%u/%u/%u expected=127/63/1/%u\n",sub,image[0],image[1],image[2],image[3],expected[3]);
    }
    check(quantize,"GL production masked-write quantization survives exact mask removal on all channels");
    check(substitution,"GL production alpha substitution writes known held bits without destination sampling");
}
