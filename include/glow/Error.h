#pragma once

#include <string>

#include <GL/glew.h>

#include <glow/glow.h>


namespace glow 
{

class GLOW_API Error
{
public:
	Error();
	Error(GLenum errorCode);

	static Error current();

    // PRE-ARB_debug_output

    /** Returns if glow internal error checking via CheckGLError macro is
        enabled or not. Note that checking is disabled via NDEBUG.
    */
    static bool isChecking();

    /** Enables or disables glow's internal error checking. This also affects
        any call to CheckGLError, but not Error::get itself. Note that 
        checking cannot be enabled in NDEBUG and for performance reasons is 
        enabled or disabled for in all contexts equally (context specific 
        checking would require platform specific context id retrieval).
    */
    static void setChecking(const bool enable);

    static bool get(const char * file, int line);
	static void clear();

	GLenum code() const;
	std::string name() const;

	bool isError() const;

	static std::string errorString(GLenum errorCode);


    // ARB_debug_output

    /** Sets up a context specific debug message error callback that outputs
        every incomming warning/error message generated by the driver.
        This should be faster than using glGetError / CheckGLError and allows
        adjustments to error type and severity per context.

        To avoid performance loss (even in DEBUG mode) this needs to be set
        manually for every context required.

        Note: this requires the GL_ARB_debug_output extension. Or at least a
        4.3 core profile. The output can be locally disabled via glEnable and
        glDisable(GL_DEBUG_OUTPUT) - this sets DEBUG_OUTPUT_SYNCHRONOUS_ARB.
    */
    static bool setupDebugOutput(
        const bool asynchronous = true);

    //static void setDebugOutput
    //,   DEBUG_SOURCE_API_ARB                              0x8246
    //    DEBUG_SOURCE_WINDOW_SYSTEM_ARB                    0x8247
    //    DEBUG_SOURCE_SHADER_COMPILER_ARB                  0x8248
    //    DEBUG_SOURCE_THIRD_PARTY_ARB                      0x8249
    //    DEBUG_SOURCE_APPLICATION_ARB                      0x824A
    //    DEBUG_SOURCE_OTHER_ARB);

protected:
    static void callback(
        GLenum source
    ,   GLenum type
    ,   GLuint id
    ,   GLenum severity
    ,   GLsizei length
    ,   const char * message
    ,   void * param);

protected:
	GLenum m_errorCode;

protected:
    static bool s_checking;
};

#ifdef NDEBUG
#define CheckGLError() false
#else
#define CheckGLError() glow::Error::get(__FILE__, __LINE__);
#endif

} // namespace glow
