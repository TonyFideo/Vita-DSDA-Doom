#ifndef DSDA_VITA_GL_COMPAT_H
#define DSDA_VITA_GL_COMPAT_H

#include <vitaGL.h>

typedef GLuint GLhandleARB;

/*
 * DSDA's OpenGL renderer still uses a number of GL 1.x/ARB/EXT spellings.
 * VitaGL exposes the equivalent functionality through core entry points and
 * enums, so keep the compatibility aliases local to the Vita build.
 */
#ifndef GL_TEXTURE0_ARB
#define GL_TEXTURE0_ARB GL_TEXTURE0
#endif
#ifndef GL_TEXTURE1_ARB
#define GL_TEXTURE1_ARB GL_TEXTURE1
#endif
#ifndef GL_TEXTURE2_ARB
#define GL_TEXTURE2_ARB GL_TEXTURE2
#endif

#ifndef GL_STATIC_DRAW_ARB
#define GL_STATIC_DRAW_ARB GL_STATIC_DRAW
#endif
#ifndef GL_VERTEX_SHADER_ARB
#define GL_VERTEX_SHADER_ARB GL_VERTEX_SHADER
#endif
#ifndef GL_FRAGMENT_SHADER_ARB
#define GL_FRAGMENT_SHADER_ARB GL_FRAGMENT_SHADER
#endif
#ifndef GL_OBJECT_COMPILE_STATUS_ARB
#define GL_OBJECT_COMPILE_STATUS_ARB GL_COMPILE_STATUS
#endif
#ifndef GL_OBJECT_LINK_STATUS_ARB
#define GL_OBJECT_LINK_STATUS_ARB GL_LINK_STATUS
#endif

#ifndef GL_FRAMEBUFFER_EXT
#define GL_FRAMEBUFFER_EXT GL_FRAMEBUFFER
#endif
#ifndef GL_RENDERBUFFER_EXT
#define GL_RENDERBUFFER_EXT GL_RENDERBUFFER
#endif
#ifndef GL_COLOR_ATTACHMENT0_EXT
#define GL_COLOR_ATTACHMENT0_EXT GL_COLOR_ATTACHMENT0
#endif
#ifndef GL_DEPTH_ATTACHMENT_EXT
#define GL_DEPTH_ATTACHMENT_EXT GL_DEPTH_ATTACHMENT
#endif
#ifndef GL_STENCIL_ATTACHMENT_EXT
#define GL_STENCIL_ATTACHMENT_EXT GL_STENCIL_ATTACHMENT
#endif
#ifndef GL_FRAMEBUFFER_COMPLETE_EXT
#define GL_FRAMEBUFFER_COMPLETE_EXT GL_FRAMEBUFFER_COMPLETE
#endif
#ifndef GL_DEPTH_STENCIL_EXT
#define GL_DEPTH_STENCIL_EXT GL_DEPTH24_STENCIL8
#endif

#ifndef GL_SOURCE0_RGB
#define GL_SOURCE0_RGB GL_SRC0_RGB
#endif
#ifndef GL_SOURCE1_RGB
#define GL_SOURCE1_RGB GL_SRC1_RGB
#endif
#ifndef GL_SOURCE0_ALPHA
#define GL_SOURCE0_ALPHA GL_SRC0_ALPHA
#endif
#ifndef GL_SOURCE1_ALPHA
#define GL_SOURCE1_ALPHA GL_SRC1_ALPHA
#endif

/* Core functions used by the renderer through legacy extension names. */
#define GLEXT_glBindFramebufferEXT glBindFramebuffer
#define GLEXT_glGenFramebuffersEXT glGenFramebuffers
#define GLEXT_glGenRenderbuffersEXT glGenRenderbuffers
#define GLEXT_glBindRenderbufferEXT glBindRenderbuffer
#define GLEXT_glRenderbufferStorageEXT glRenderbufferStorage
#define GLEXT_glFramebufferRenderbufferEXT glFramebufferRenderbuffer
#define GLEXT_glFramebufferTexture2DEXT glFramebufferTexture2D
#define GLEXT_glCheckFramebufferStatusEXT glCheckFramebufferStatus
#define GLEXT_glDeleteFramebuffersEXT glDeleteFramebuffers
#define GLEXT_glDeleteRenderbuffersEXT glDeleteRenderbuffers

#define GLEXT_glActiveTextureARB glActiveTexture
#define GLEXT_glClientActiveTextureARB glClientActiveTexture
#define GLEXT_glMultiTexCoord2fARB glMultiTexCoord2f
#define GLEXT_glMultiTexCoord2fvARB glMultiTexCoord2fv

#define GLEXT_glGenBuffersARB glGenBuffers
#define GLEXT_glDeleteBuffersARB glDeleteBuffers
#define GLEXT_glBindBufferARB glBindBuffer
#define GLEXT_glBufferDataARB glBufferData

#define GLEXT_glShaderSourceARB glShaderSource
#define GLEXT_glCompileShaderARB glCompileShader
#define GLEXT_glCreateShaderObjectARB glCreateShader
#define GLEXT_glCreateProgramObjectARB glCreateProgram
#define GLEXT_glAttachObjectARB glAttachShader
#define GLEXT_glLinkProgramARB glLinkProgram
#define GLEXT_glGetUniformLocationARB glGetUniformLocation
#define GLEXT_glUniform1fARB glUniform1f
#define GLEXT_glUniform2fARB glUniform2f
#define GLEXT_glUniform1iARB glUniform1i
#define GLEXT_glUseProgramObjectARB glUseProgram

static inline void dsda_vita_glGetObjectParameterivARB(
  GLuint object,
  GLenum pname,
  GLint *params
)
{
  if (pname == GL_OBJECT_LINK_STATUS_ARB)
    glGetProgramiv(object, GL_LINK_STATUS, params);
  else
    glGetShaderiv(object, GL_COMPILE_STATUS, params);
}

static inline void dsda_vita_glGetInfoLogARB(
  GLuint object,
  GLsizei max_length,
  GLsizei *length,
  GLchar *info_log
)
{
  (void)object;
  if (length)
    *length = 0;
  if (info_log && max_length > 0)
    info_log[0] = '\0';
}

#define GLEXT_glGetObjectParameterivARB dsda_vita_glGetObjectParameterivARB
#define GLEXT_glGetInfoLogARB dsda_vita_glGetInfoLogARB

#endif
