
#include <globjects/ProgramPipeline.h>

#include <iostream>

#include <glbinding/gl/enum.h>
#include <glbinding/gl/boolean.h>
#include <glbinding/gl/functions.h>

#include <globjects/Program.h>
#include <globjects/Resource.h>


namespace globjects
{


ProgramPipeline::ProgramPipeline()
: Object(std::unique_ptr<IDResource>(new ProgramPipelineResource()))
, m_dirty(true)
{
}

ProgramPipeline::~ProgramPipeline()
{
    if (0 == id())
    {
        for (auto & program : m_programs)
        {
            program->deregisterListener(this);
        }
    }
    else
    {
        for (auto program : std::set<Program *>(m_programs))
        {
            releaseProgram(program);
        }
    }

    while (!m_programSubjects.empty())
    {
        // calls removeSubject
        (*m_programSubjects.begin())->deregisterListener(this);
    }
}

void ProgramPipeline::notifyChanged(const Program *)
{
}

void ProgramPipeline::addSubject(Program * subject)
{
    m_programSubjects.insert(subject);
}

void ProgramPipeline::removeSubject(Program * subject)
{
    assert(subject != nullptr);

    const auto it = m_programSubjects.find(subject);

    if (it == m_programSubjects.end())
    {
        return;
    }

    m_programSubjects.erase(it);
    subject->deregisterListener(this);
}

void ProgramPipeline::use() const
{
    if (m_dirty)
    {
        for (const Program * program : m_programs)
        {
            program->link();
        }

        const_cast<ProgramPipeline *>(this)->m_dirty = false;

        checkUseStatus();
    }

    gl::glUseProgram(0);
    gl::glBindProgramPipeline(id());
}

void ProgramPipeline::release()
{
    gl::glBindProgramPipeline(0);
}

void ProgramPipeline::useStages(Program * program, gl::UseProgramStageMask stages)
{
    program->setParameter(gl::GL_PROGRAM_SEPARABLE, gl::GL_TRUE);

    program->registerListener(this);
    m_programs.emplace(program);

    program->link();

    gl::glUseProgramStages(id(), stages, program->id());

    invalidate();
}

void ProgramPipeline::releaseStages(gl::UseProgramStageMask stages)
{
    gl::glUseProgramStages(id(), stages, 0);

    invalidate();
}

void ProgramPipeline::releaseProgram(Program * program)
{
    program->deregisterListener(this);
    m_programs.erase(program);

    invalidate();
}

bool ProgramPipeline::isValid() const
{
    return get(gl::GL_VALIDATE_STATUS) == 1;
}

void ProgramPipeline::validate() const
{
    gl::glValidateProgramPipeline(id());
}

void ProgramPipeline::invalidate()
{
    m_dirty = true;
}

bool ProgramPipeline::checkUseStatus() const
{
    validate();

    if (!isValid())
    {
        critical() << "Use error:" << std::endl << infoLog();

        return false;
    }

    return true;
}

gl::GLint ProgramPipeline::get(const gl::GLenum pname) const
{
    gl::GLint value = 0;
    gl::glGetProgramPipelineiv(id(), pname, &value);

    return value;
}

std::string ProgramPipeline::infoLog() const
{
    gl::GLint length = get(gl::GL_INFO_LOG_LENGTH);

    if (length == 0)
    {
        return std::string();
    }

    std::vector<char> log(length);

    gl::glGetProgramPipelineInfoLog(id(), length, &length, log.data());

    return std::string(log.data(), length);
}

gl::GLenum ProgramPipeline::objectType() const
{
    return gl::GL_PROGRAM_PIPELINE;
}


} // namespace globjects
