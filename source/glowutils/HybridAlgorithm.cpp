#include <glowutils/HybridAlgorithm.h>

#include <glow/Program.h>
#include <glow/FrameBufferObject.h>
#include <glow/Texture.h>
#include <glow/RenderBufferObject.h>
#include <glow/Buffer.h>
#include <glow/NamedStrings.h>
#include <glow/Array.h>

#include <glowutils/File.h>
#include <glowutils/Camera.h>
#include <glowutils/ScreenAlignedQuad.h>

namespace glowutils {

namespace {

const int ABUFFER_SIZE = 4;
const int VISIBILITY_KTAB_SIZE = ABUFFER_SIZE + 1;

}

void HybridAlgorithm::initialize(const std::string & transparencyShaderFilePath, glow::Shader *vertexShader, glow::Shader *geometryShader) {
    glow::NamedStrings::createNamedString("/transparency/hybrid_definitions", "const int ABUFFER_SIZE = " + std::to_string(ABUFFER_SIZE) + ";");
	glow::NamedStrings::createNamedString("/transparency/hybrid.glsl", new glowutils::File(transparencyShaderFilePath + "hybrid.glsl"));

	m_opaqueProgram = new glow::Program;
    m_opaqueProgram->attach(vertexShader);
	m_opaqueProgram->attach(glowutils::createShaderFromFile(GL_FRAGMENT_SHADER, transparencyShaderFilePath + "hybrid_opaque.frag"));
	if (geometryShader != nullptr) m_opaqueProgram->attach(geometryShader);

	m_depthKTabProgram = new glow::Program;
    m_depthKTabProgram->attach(vertexShader);
	m_depthKTabProgram->attach(glowutils::createShaderFromFile(GL_FRAGMENT_SHADER, transparencyShaderFilePath + "hybrid_depthktab.frag"));
	if (geometryShader != nullptr) m_depthKTabProgram->attach(geometryShader);

	m_visibilityKTabProgram = new glow::Program;
	m_visibilityKTabProgram->attach(glowutils::createShaderFromFile(GL_COMPUTE_SHADER, transparencyShaderFilePath + "hybrid_visibilityktab.comp"));

	m_colorProgram = new glow::Program;
    m_colorProgram->attach(vertexShader);
	m_colorProgram->attach(glowutils::createShaderFromFile(GL_FRAGMENT_SHADER, transparencyShaderFilePath + "hybrid_color.frag"));
	if (geometryShader != nullptr) m_colorProgram->attach(geometryShader);

	m_depthBuffer = new glow::RenderBufferObject;
    m_opaqueBuffer = createColorTex();
    m_coreBuffer = createColorTex();
    m_accumulationBuffer = createColorTex();
    m_colorBuffer = createColorTex();
	m_depthKTab = new glow::Buffer(GL_SHADER_STORAGE_BUFFER);
	m_visibilityKTab = new glow::Buffer(GL_SHADER_STORAGE_BUFFER);
	m_depthComplexityBuffer = new glow::Buffer(GL_SHADER_STORAGE_BUFFER);

	m_prepassFbo = new glow::FrameBufferObject;
    m_prepassFbo->attachTexture2D(GL_COLOR_ATTACHMENT0, m_opaqueBuffer.get());
    m_prepassFbo->attachRenderBuffer(GL_DEPTH_ATTACHMENT, m_depthBuffer.get());

	m_colorFbo = new glow::FrameBufferObject;
    m_colorFbo->attachTexture2D(GL_COLOR_ATTACHMENT0, m_coreBuffer.get());
    m_colorFbo->attachTexture2D(GL_COLOR_ATTACHMENT1, m_accumulationBuffer.get());
    m_colorFbo->attachRenderBuffer(GL_DEPTH_ATTACHMENT, m_depthBuffer.get());
    m_colorFbo->setDrawBuffers({ GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 });

	m_compositionQuad = new glowutils::ScreenAlignedQuad(glowutils::createShaderFromFile(GL_FRAGMENT_SHADER, transparencyShaderFilePath + "hybrid_post.frag"));
    m_compositionFbo = new glow::FrameBufferObject;
    m_compositionFbo->attachTexture2D(GL_COLOR_ATTACHMENT0, m_colorBuffer.get());
    m_compositionFbo->setDrawBuffer(GL_COLOR_ATTACHMENT0);
}

void HybridAlgorithm::draw(const DrawFunction& drawFunction, glowutils::Camera* camera, int width, int height) {
    glViewport(0, 0, width, height);
    camera->setViewport(width, height);

    m_prepassFbo->bind();

    //
    // render opaque geometry
    //
    m_prepassFbo->setDrawBuffer(GL_COLOR_ATTACHMENT0);
    m_prepassFbo->clear(GL_DEPTH_BUFFER_BIT);
    m_prepassFbo->clearBuffer(GL_COLOR, 0, glm::vec4(1.0f));

    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    m_opaqueProgram->setUniform("viewprojectionmatrix", camera->viewProjection());
    m_opaqueProgram->setUniform("normalmatrix", camera->normal());
    m_opaqueProgram->use();

    drawFunction(m_opaqueProgram.get());

    //
    // render translucent geometry into depth k-TAB (store depth and alpha for the first (minimum depth) k fragments)
    //
    m_prepassFbo->setDrawBuffer(GL_NONE);
    glDepthMask(GL_FALSE);

	static glow::Array<unsigned int> initialDepthKTab;
    initialDepthKTab.resize(width * height * ABUFFER_SIZE, std::numeric_limits<unsigned int>::max());
    m_depthKTab->setData(initialDepthKTab, GL_DYNAMIC_DRAW);
    m_depthKTab->bindBase(GL_SHADER_STORAGE_BUFFER, 0);

    m_depthKTabProgram->setUniform("viewprojectionmatrix", camera->viewProjection());
    m_depthKTabProgram->setUniform("normalmatrix", camera->normal());
    m_depthKTabProgram->setUniform("screenSize", glm::ivec2(width, height));
    m_depthKTabProgram->use();

    drawFunction(m_depthKTabProgram.get());

    glDepthMask(GL_TRUE);

    m_prepassFbo->unbind();

    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    //
    // compute visibility k-TAB: The alpha visibility for each of the first k fragments.
	// The alpha blended color for the k front most fragments of a single pixel is calculated by the following equation:
	//	acc_0 = src_0 (src_i is the i-th k-Tab entry of the pixel)
	//	for i = 1 to k: 
	//		factor = 1.0 - acc_(i-1).a
	//		acc_i = acc_(i-1) + src_i * factor
	//	
	// This pass computes "factor" for each fragment so that in the final path the colors of the k fragments can be combined order independent
	static glow::Array<float> initialVisibilityKTab;
    initialVisibilityKTab.resize(width * height * VISIBILITY_KTAB_SIZE, 0.0f);
    m_visibilityKTab->setData(initialVisibilityKTab, GL_DYNAMIC_DRAW);
    m_visibilityKTab->bindBase(GL_SHADER_STORAGE_BUFFER, 1);

    m_visibilityKTabProgram->setUniform("dimension", width * height);
    m_visibilityKTabProgram->dispatchCompute(width * height / 32 + 1, 1, 1);

    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    //
    // render translucent colors
    //
    m_colorFbo->bind();
    m_colorFbo->clearBuffer(GL_COLOR, 0, glm::vec4(0.0f));
    m_colorFbo->clearBuffer(GL_COLOR, 1, glm::vec4(0.0f));

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glDepthMask(GL_FALSE);

	static glow::Array<unsigned int> initialDepthComplexity;
    initialDepthComplexity.resize(width * height, 0);
    m_depthComplexityBuffer->setData(initialDepthComplexity, GL_DYNAMIC_DRAW);
    m_depthComplexityBuffer->bindBase(GL_SHADER_STORAGE_BUFFER, 2);

    m_colorProgram->setUniform("viewprojectionmatrix", camera->viewProjection());
    m_colorProgram->setUniform("normalmatrix", camera->normal());
    m_colorProgram->setUniform("screenSize", glm::ivec2(width, height));
    m_colorProgram->use();

    drawFunction(m_colorProgram.get());

    glDepthMask(GL_TRUE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_BLEND);

    m_colorFbo->unbind();

    //
    // compose core and tail
    //
    m_compositionFbo->bind();
    m_compositionFbo->clear(GL_COLOR_BUFFER_BIT);

    m_opaqueBuffer->bind(GL_TEXTURE0);
    m_coreBuffer->bind(GL_TEXTURE1);
    m_accumulationBuffer->bind(GL_TEXTURE2);

    m_compositionQuad->program()->setUniform("screenSize", glm::ivec2(width, height));
    m_compositionQuad->program()->setUniform("opaqueBuffer", 0);
    m_compositionQuad->program()->setUniform("coreBuffer", 1);
    m_compositionQuad->program()->setUniform("accumulationBuffer", 2);
    m_compositionQuad->draw();

    m_opaqueBuffer->unbind(GL_TEXTURE0);
    m_coreBuffer->unbind(GL_TEXTURE1);
    m_accumulationBuffer->unbind(GL_TEXTURE2);

    m_compositionFbo->unbind();
}

void HybridAlgorithm::resize(int width, int height) {
    m_depthBuffer->storage(GL_DEPTH_COMPONENT, width, height);
    m_opaqueBuffer->image2D(0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    m_colorBuffer->image2D(0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    m_accumulationBuffer->image2D(0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
    m_coreBuffer->image2D(0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
}

} // namespace glow
