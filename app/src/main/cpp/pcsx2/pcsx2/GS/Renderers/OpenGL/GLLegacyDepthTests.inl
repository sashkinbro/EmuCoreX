// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Included inside TestFramebufferFetch with a complete 4x4 UNORM FBO/VAO.
{
    const bool oldClip=device.m_has_clip_control;
    const ScopedGuard restoreClip([&](){device.m_has_clip_control=oldClip;});
    // fetch_raw_depth consumes ChannelShuffleOffset from the production UBO;
    // sample_depth also consumes scale/clamp uniforms. Bind a complete zeroed
    // block instead of relying on undefined unbound-uniform-buffer reads.
    GLuint constants=0;glGenBuffers(1,&constants);
    GLint oldConstants=0;glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING,0,&oldConstants);
    glBindBuffer(GL_UNIFORM_BUFFER,constants);std::array<u8,1024> constantBytes{};
    glBufferData(GL_UNIFORM_BUFFER,constantBytes.size(),constantBytes.data(),GL_STATIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER,0,constants);
    const ScopedGuard freeConstants([&](){glBindBufferBase(GL_UNIFORM_BUFFER,0,oldConstants);glDeleteBuffers(1,&constants);});
    GLuint depthImage=0;glGenTextures(1,&depthImage);
    const ScopedGuard deleteDepth([&](){glDeleteTextures(1,&depthImage);});
    std::array<float,16> stored;stored.fill(0.125f+0x1p-20f);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,depthImage);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D,0,GL_R32F,4,4,0,GL_RED,GL_FLOAT,stored.data());
    glActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_2D,depthImage);
    bool raw=true,rgba=true,rgb5=true;u32 cases=0;
    for(bool clip:{false,true})for(bool framebuffer:{false,true})for(u32 format=0;format<3;format++)
    {
        device.m_has_clip_control=clip;
        PSSelector ps{};ps.tfx=0;ps.iip=true;ps.no_color1=true;ps.tex_is_fb=framebuffer;ps.depth_fmt=format;
        const char* probe=format?"\nvoid main(){o_col0=sample_depth(vec2(0.))/255.;}\n":
            "\nvoid main(){uint d=fetch_raw_depth();o_col0=vec4(uvec4(d&255u,(d>>8)&255u,(d>>16)&255u,d>>24))/255.;}\n";
        const GLuint fs=Compile(GL_FRAGMENT_SHADER,ProbeMain(device.GetPSSource(ps,GLFramebufferFetchPlan{}),probe));
        const GLuint prog=Link(vertex,fs);if(fs)glDeleteShader(fs);
        if(!prog){raw=rgba=rgb5=false;continue;}
        const ScopedGuard freeProgram([&](){glDeleteProgram(prog);});
        glUseProgram(prog);glDrawArrays(GL_TRIANGLES,0,3);
        std::array<u8,64> image{};glReadPixels(0,0,4,4,GL_RGBA,GL_UNSIGNED_BYTE,image.data());
        // Exact binary fixture: (1/8+1/2^20)*2^24=0x200010;
        //                         *2^32=0x20001000.
        const std::array<u8,4> expected=format==2?(clip?std::array<u8,4>{0,0,32,0}:std::array<u8,4>{128,0,0,0}):
            clip?std::array<u8,4>{0,16,0,32}:std::array<u8,4>{16,0,32,0};
        bool exact=glGetError()==GL_NO_ERROR;
        for(size_t i=0;i<image.size();i++)exact&=image[i]==expected[i%4];
        if(format==0)raw&=exact;else if(format==1)rgba&=exact;else rgb5&=exact;
        if(!exact)std::fprintf(stderr,"GL_LEGACY_DEPTH clip=%u fb=%u format=%u rgba=%u/%u/%u/%u expected=%u/%u/%u/%u\n",
            clip,framebuffer,format,image[0],image[1],image[2],image[3],expected[0],expected[1],expected[2],expected[3]);
        cases++;
    }
    // Observe the full production ps_main depth store at a value below one
    // 32-bit GS step. A tiny viewport depth range produces an exactly
    // representable 2^-33 without near-plane subtraction losing precision.
    GLuint zImage=0;glGenTextures(1,&zImage);
    const ScopedGuard freeZ([&](){glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,0,0);
        glDepthRangef(0.,1.);glDisable(GL_DEPTH_TEST);glDeleteTextures(1,&zImage);});
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,zImage);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D,0,GL_DEPTH_COMPONENT32F,4,4,0,GL_DEPTH_COMPONENT,GL_FLOAT,nullptr);
    bool zfloor=true;u32 zCases=0;
    for(bool clip:{false,true})
    {
        device.m_has_clip_control=clip;
        PSSelector ps{};ps.tfx=4;ps.iip=true;ps.no_color1=true;ps.zfloor=true;
        const GLuint fs=Compile(GL_FRAGMENT_SHADER,device.GetPSSource(ps,GLFramebufferFetchPlan{}));
        const GLuint write=Link(vertex,fs);if(fs)glDeleteShader(fs);
        if(!write){zfloor=false;continue;}const ScopedGuard freeWrite([&](){glDeleteProgram(write);});
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,zImage,0);
        if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE){zfloor=false;continue;}
        glDepthRangef(0.,0x1p-32f);glEnable(GL_DEPTH_TEST);glDepthFunc(GL_ALWAYS);glDepthMask(GL_TRUE);
        glUseProgram(write);glDrawArrays(GL_TRIANGLES,0,3);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,0,0);glDisable(GL_DEPTH_TEST);glDepthRangef(0.,1.);
        PSSelector read{};read.tfx=0;read.iip=true;read.no_color1=true;
        const GLuint readFS=Compile(GL_FRAGMENT_SHADER,ProbeMain(device.GetPSSource(read,GLFramebufferFetchPlan{}),
            "\nvoid main(){o_col0=vec4(texelFetch(TextureSampler,ivec2(gl_FragCoord.xy),0).r*exp2(34.)/255.,0.,0.,1.);}\n"));
        const GLuint sample=Link(vertex,readFS);if(readFS)glDeleteShader(readFS);
        if(!sample){zfloor=false;continue;}const ScopedGuard freeSample([&](){glDeleteProgram(sample);});
        glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,zImage);glUseProgram(sample);glDrawArrays(GL_TRIANGLES,0,3);
        std::array<u8,64> image{};glReadPixels(0,0,4,4,GL_RGBA,GL_UNSIGNED_BYTE,image.data());
        bool exact=glGetError()==GL_NO_ERROR;for(size_t i=0;i<image.size();i+=4)exact&=image[i]==(clip?0:2);
        zfloor&=exact;zCases++;
        if(!exact)std::fprintf(stderr,"GL_ZFLOOR clip=%u red=%u expected=%u\n",clip,image[0],clip?0:2);
    }
    check(zfloor&&zCases==2,"GL full production depth store floors 32-bit clip depth and preserves substep legacy depth");
    check(raw,"GL actual raw depth helper decodes clip-control 32-bit and legacy 24-bit texture and framebuffer sources");
    check(rgba,"GL actual depth32-to-RGBA conversion respects actual clip-control state for both source bindings");
    check(rgb5,"GL actual depth16-to-RGB5A1 conversion respects actual clip-control state for both source bindings");
    std::printf("GL_LEGACY_DEPTH cases=%u\n",cases);
}
