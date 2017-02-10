#include "sge/core/deferred_shading_renderer.h"

#include "sge/core/shader_manager.h"
#include "sge/core/texture_unit.h"
#include "sge/core/create_texture.h"
#include "sge/object_extensions/light_source.h"
#include "sge/core/debug_input.h"

namespace sge { namespace core {

DeferredShadingRenderer::DeferredShadingRenderer(
  PerspectiveCamera& camera, int framebuffer_width, int framebuffer_height) :
  _window_width(framebuffer_width),
  _window_height(framebuffer_height),
  _camera(camera)
{
  initializeShaders();
  initializeFramebuffers(framebuffer_width, framebuffer_height);

  // Camera will save uniform locations for the shaders
  _camera.addToShader(_gbuffer_program->id());
  _camera.addToShader(_shading_program_point_lights->id());
  _camera.addToShader(_shading_program_directional_lights->id());
  _camera.addToShader(_cube_map_program->id());
}

DeferredShadingRenderer::~DeferredShadingRenderer()
{

}

void DeferredShadingRenderer::setWindowResolution(int width, int height)
{
  _window_width = width;
  _window_height = height;
  _camera.setAspectRatio( static_cast<float>(width) / height);
}

void DeferredShadingRenderer::setSkyBox(std::shared_ptr<RenderableCubeMap> sky_box)
{
  _sky_box = sky_box;
}

void DeferredShadingRenderer::render(Object3D& scene)
{
  // Submit all objects in the scene to the lists of renderable objects
  scene.submit(*this);
  // Update cameras uniforms for all shaders
  _camera.execute();

  renderGeometryBuffer(*_geometry_fbo_quad);
  renderLightSources(*_light_fbo_quad);

  // This will allow reflections to have roughness by sampling from mip-map
  _light_fbo_quad->generateMipMaps();

  renderReflections(*_final_irradiance_fbo_quad);

  // This will allow depth of field by sampling from mip-map
  _final_irradiance_fbo_quad->generateMipMaps();

  renderHighlights(*_post_process_fbo_quad);

  // This will allow smooth blooming by sampling from mip-map
  _post_process_fbo_quad->generateMipMaps();

  renderPostProcess(*_final_pass_through_fbo_quad);

  renderFinalPassthroughToScreen();

  checkForErrors();
}

void DeferredShadingRenderer::initializeShaders()
{
  _gbuffer_program = std::make_shared<ShaderProgram>(
    "gbuffer_program",
    "../../shaders/deferred_shading/geometry_pass.vert",
    nullptr,
    nullptr,
    nullptr,
    "../../shaders/deferred_shading/geometry_pass.frag");
  _shading_program_point_lights = std::make_shared<ShaderProgram>(
    "shading_program_point_lights",
    "../../shaders/deferred_shading/shading_pass.vert",
    nullptr,
    nullptr,
    nullptr,
    "../../shaders/deferred_shading/shading_pass_point_light.frag");
  _shading_program_directional_lights = std::make_shared<ShaderProgram>(
    "shading_program_directional_lights",
    "../../shaders/deferred_shading/shading_pass.vert",
    nullptr,
    nullptr,
    nullptr,
    "../../shaders/deferred_shading/shading_pass_directional_light.frag");
  _shading_program_environment_diffuse = std::make_shared<ShaderProgram>(
    "shading_program_environment_diffuse",
    "../../shaders/deferred_shading/shading_pass.vert",
    nullptr,
    nullptr,
    nullptr,
    "../../shaders/deferred_shading/shading_pass_environment_diffuse.frag");
  _shading_program_reflections = std::make_shared<ShaderProgram>(
    "shading_program_reflection",
    "../../shaders/deferred_shading/shading_pass.vert",
    nullptr,
    nullptr,
    nullptr,
    "../../shaders/deferred_shading/shading_pass_reflection.frag");
  _shading_program_irradiance = std::make_shared<ShaderProgram>(
    "shading_program_irradiance",
    "../../shaders/deferred_shading/shading_pass.vert",
    nullptr,
    nullptr,
    nullptr,
    "../../shaders/deferred_shading/shading_pass_irradiance.frag");
  _cube_map_program = std::make_shared<ShaderProgram>(
    "cube_map_program",
    "../../shaders/deferred_shading/cube_map.vert",
    nullptr,
    nullptr,
    nullptr,
    "../../shaders/deferred_shading/cube_map.frag");
  _output_highlights_program = std::make_shared<ShaderProgram>(
    "output_highlights_program",
    "../../shaders/deferred_shading/shading_pass.vert",
    nullptr,
    nullptr,
    nullptr,
    "../../shaders/deferred_shading/shading_pass_output_highlights.frag");
  _post_process_program = std::make_shared<ShaderProgram>(
    "post_process_program",
    "../../shaders/deferred_shading/shading_pass.vert",
    nullptr,
    nullptr,
    nullptr,
    "../../shaders/deferred_shading/shading_pass_post_process.frag");
  _final_pass_through_program = std::make_shared<ShaderProgram>(
    "final_pass_through_program",
    "../../shaders/deferred_shading/shading_pass.vert",
    nullptr,
    nullptr,
    nullptr,
    "../../shaders/deferred_shading/final_pass_through.frag");
}

void DeferredShadingRenderer::initializeFramebuffers(
  int framebuffer_width, int framebuffer_height)
{
  // Create render textures to be used in G-Buffer
  FrameBufferQuad::RenderTexture albedo_render_tex =
  {
    std::make_shared<Texture>(
      glm::uvec3(framebuffer_width, framebuffer_height, 1),
      Texture::Format::RGBA, GL_RGBA, GL_UNSIGNED_BYTE,
      Texture::FilterMode::Nearest,
      Texture::WrappingMode::ClampToEdge),
      GL_COLOR_ATTACHMENT0,
      "albedo_buffer"
  };
  FrameBufferQuad::RenderTexture position_render_tex =
  {
    std::make_shared<Texture>(
      glm::uvec3(framebuffer_width, framebuffer_height, 1),
      Texture::Format::RGB, GL_RGB16F, GL_HALF_FLOAT,
      Texture::FilterMode::Nearest,
      Texture::WrappingMode::ClampToEdge),
      GL_COLOR_ATTACHMENT1,
      "position_buffer"
  };
  FrameBufferQuad::RenderTexture normal_render_tex =
  {
    std::make_shared<Texture>(
      glm::uvec3(framebuffer_width, framebuffer_height, 1),
      Texture::Format::RGB, GL_RGB16F, GL_HALF_FLOAT,
      Texture::FilterMode::Nearest,
      Texture::WrappingMode::ClampToEdge),
      GL_COLOR_ATTACHMENT2,
      "normal_buffer"
  };
  FrameBufferQuad::RenderTexture material_render_tex =
  {
    std::make_shared<Texture>(
      glm::uvec3(framebuffer_width, framebuffer_height, 1),
      Texture::Format::RGB, GL_RGB16F, GL_HALF_FLOAT,
      Texture::FilterMode::Nearest,
      Texture::WrappingMode::ClampToEdge),
      GL_COLOR_ATTACHMENT3,
      "material_buffer"
  };
  
  // Create render textures to be used in Light-Buffer
  // (irradiance before screen space reflections)
  FrameBufferQuad::RenderTexture irradiance_render_tex =
  {
    std::make_shared<Texture>(
      glm::uvec3(framebuffer_width, framebuffer_height, 1),
      Texture::Format::RGBA, GL_RGBA16F, GL_HALF_FLOAT,
      Texture::FilterMode::LinearMipMap,
      Texture::WrappingMode::ClampToEdge),
      GL_COLOR_ATTACHMENT0,
      "irradiance_buffer"
  };

  // Total irradiance after screen space reflections
  FrameBufferQuad::RenderTexture final_irradiance_render_tex =
  {
    std::make_shared<Texture>(
      glm::uvec3(framebuffer_width, framebuffer_height, 1),
      Texture::Format::RGB, GL_RGB16F, GL_HALF_FLOAT,
      Texture::FilterMode::LinearMipMap,
      Texture::WrappingMode::ClampToEdge),
      GL_COLOR_ATTACHMENT0,
      "final_irradiance_buffer"
  };

  // Render textures for post processing
  FrameBufferQuad::RenderTexture bloom_render_tex =
  {
    std::make_shared<Texture>(
      glm::uvec3(framebuffer_width / 2, framebuffer_height / 2, 1),
      Texture::Format::RGB, GL_RGB16F, GL_HALF_FLOAT,
      Texture::FilterMode::LinearMipMap,
      Texture::WrappingMode::ClampToEdge),
      GL_COLOR_ATTACHMENT0,
      "bloom_buffer"
  };

  // Final output
  FrameBufferQuad::RenderTexture final_render_color_tex =
  {
    std::make_shared<Texture>(
      glm::uvec3(framebuffer_width, framebuffer_height, 1),
      Texture::Format::RGB, GL_RGB, GL_UNSIGNED_BYTE,
      Texture::FilterMode::Linear,
      Texture::WrappingMode::ClampToEdge),
      GL_COLOR_ATTACHMENT0,
      "color_buffer"
  };

  // Generate the framebuffer quads
  _geometry_fbo_quad = std::make_unique<FrameBufferQuad>(
    framebuffer_width, framebuffer_height,
    std::vector<FrameBufferQuad::RenderTexture>{
      albedo_render_tex,
      position_render_tex,
      normal_render_tex,
      material_render_tex },
      FrameBufferQuad::UseDepthBuffer::YES);

  _light_fbo_quad = std::make_unique<FrameBufferQuad>(
    framebuffer_width, framebuffer_height,
    std::vector<FrameBufferQuad::RenderTexture>{irradiance_render_tex});

  _final_irradiance_fbo_quad = std::make_unique<FrameBufferQuad>(
    framebuffer_width, framebuffer_height,
    std::vector<FrameBufferQuad::RenderTexture>{
      final_irradiance_render_tex});

  _post_process_fbo_quad = std::make_unique<FrameBufferQuad>(
    framebuffer_width / 2, framebuffer_height / 2,
    std::vector<FrameBufferQuad::RenderTexture>{bloom_render_tex});

  _final_pass_through_fbo_quad = std::make_unique<FrameBufferQuad>(
    framebuffer_width, framebuffer_height,
    std::vector<FrameBufferQuad::RenderTexture>{final_render_color_tex});
}

void DeferredShadingRenderer::renderGeometryBuffer(FrameBufferQuad& geometry_buffer)
{
  geometry_buffer.bindFBO();
  glViewport(0,0, geometry_buffer.width(), geometry_buffer.height());
  glClearColor(0.0, 0.0, 0.0, 0.0);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  glEnable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);

  _gbuffer_program->pushUsage();  
  for (auto renderable : _renderables_to_render)
    renderable->render();
  _renderables_to_render.clear();
  _gbuffer_program->popUsage();

  geometry_buffer.unbindFBO();
}

void DeferredShadingRenderer::renderLightSources(FrameBufferQuad& light_buffer)
{
  // Render to irradiance buffer
  light_buffer.bindFBO();
  // Setup for rendering light sources to the light_buffer.rame buffer
  glViewport(0,0, light_buffer.width(), light_buffer.height());
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glDisable(GL_DEPTH_TEST);
  glBlendFunc(GL_ONE, GL_ONE);
  glEnable(GL_BLEND);
  
  // Render light sources
  renderPointLights();
  renderDirectionalLights();

  if (_sky_box)
  {
    renderDiffuseEnvironmentLights();
    renderSkyBox();
  }
  light_buffer.unbindFBO();
}

void DeferredShadingRenderer::renderReflections(FrameBufferQuad& irradiance_buffer)
{
  // Render to final irradiance buffer (reflections)
  irradiance_buffer.bindFBO(); {
    glViewport(0,0,
      irradiance_buffer.width(),
      irradiance_buffer.height());
    glDisable(GL_BLEND);
    
    if (_sky_box) // Renders screen space reflections and cube map reflections
      renderScreenSpaceReflections();
    else // This is just a pass through
      renderIrradiance();
  } irradiance_buffer.unbindFBO();
}

void DeferredShadingRenderer::renderHighlights(FrameBufferQuad& post_process_buffer)
{
  post_process_buffer.bindFBO();
  glViewport(0,0,
    post_process_buffer.width(),
    post_process_buffer.height());
  glClear(GL_COLOR_BUFFER_BIT);

  _output_highlights_program->pushUsage();
  glUniform2i(
    glGetUniformLocation(ShaderProgram::currentProgramId(), "window_size"),
    post_process_buffer.width(), post_process_buffer.height());
  _final_irradiance_fbo_quad->bindTextures();
  _final_irradiance_fbo_quad->render();
  _final_irradiance_fbo_quad->freeTextureUnits();
  _output_highlights_program->popUsage();
  post_process_buffer.unbindFBO();
}

void DeferredShadingRenderer::renderPostProcess(FrameBufferQuad& final_buffer)
{
  // Perform post processing, rendering to final buffer
  final_buffer.bindFBO();
  glViewport(0,0,
    final_buffer.width(),
    final_buffer.height());
  
  _post_process_program->pushUsage();
  glUniform2i(
    glGetUniformLocation(ShaderProgram::currentProgramId(), "window_size"),
    final_buffer.width(),
    final_buffer.height());
  glUniform2i(
    glGetUniformLocation(ShaderProgram::currentProgramId(), "bloom_buffer_base_size"),
    _post_process_fbo_quad->width(),
    _post_process_fbo_quad->height());
  glm::mat4 P_inv = glm::inverse(_camera.projectionTransform());
  glUniformMatrix4fv(
    glGetUniformLocation(ShaderProgram::currentProgramId(), "P_inv"), 1, GL_FALSE,
    &P_inv[0][0]);
  glUniform1f(
    glGetUniformLocation(ShaderProgram::currentProgramId(), "focal_length"),
    _camera.focalLength() / 1000.0f); // Convert from mm to m
  glUniform1f(
    glGetUniformLocation(ShaderProgram::currentProgramId(), "focus"),
    _camera.focus() / 1000.0f); // Convert from mm to m


  float diagonal = _camera.diagonal() / 1000.0f;
  float window_diagonal =
    sqrt(pow(final_buffer.width(), 2) + pow(final_buffer.height(), 2));  
  float inv_focal_ratio_in_pixels =
    1.0f / (diagonal * _camera.focalRatio()) * window_diagonal;

  glUniform1f(
    glGetUniformLocation(ShaderProgram::currentProgramId(),
      "inv_focal_ratio_in_pixels"),
    inv_focal_ratio_in_pixels);

  _final_irradiance_fbo_quad->bindTextures();
  _post_process_fbo_quad->bindTextures();
  _geometry_fbo_quad->bindTextures();

  _final_irradiance_fbo_quad->render();
  
  _final_irradiance_fbo_quad->freeTextureUnits();
  _post_process_fbo_quad->freeTextureUnits();
  _geometry_fbo_quad->freeTextureUnits();

  _post_process_program->popUsage();

  final_buffer.unbindFBO();
}

void DeferredShadingRenderer::renderFinalPassthroughToScreen()
{
  glViewport(0,0, _window_width, _window_height);
  _final_pass_through_program->pushUsage();
  glUniform2i(
    glGetUniformLocation(ShaderProgram::currentProgramId(), "window_size"),
    _window_width, _window_height);
  _final_pass_through_fbo_quad->bindTextures();
  _final_pass_through_fbo_quad->render();
  _final_pass_through_fbo_quad->freeTextureUnits();
  _final_pass_through_program->popUsage();
}

void DeferredShadingRenderer::renderPointLights()
{
  _shading_program_point_lights->pushUsage();
  glUniformMatrix4fv(
    glGetUniformLocation(ShaderProgram::currentProgramId(), "P_frag"), 1, GL_FALSE,
    &_camera.projectionTransform()[0][0]);
  
  _geometry_fbo_quad->bindTextures();
  for (auto it : _point_light_sources_to_render)
  {
    it->render(_camera);
  }
  _geometry_fbo_quad->freeTextureUnits();
  _point_light_sources_to_render.clear();
  _shading_program_point_lights->popUsage();
}

void DeferredShadingRenderer::renderDirectionalLights()
{
  _shading_program_directional_lights->pushUsage();
  glUniformMatrix4fv(
    glGetUniformLocation(ShaderProgram::currentProgramId(), "P_frag"), 1, GL_FALSE,
    &_camera.projectionTransform()[0][0]);
  _geometry_fbo_quad->bindTextures();
  for (auto it : _directional_light_sources_to_render)
  {
    it->render(_camera);
  }
  _geometry_fbo_quad->freeTextureUnits();
  _directional_light_sources_to_render.clear();
  _shading_program_directional_lights->popUsage();
}

void DeferredShadingRenderer::renderDiffuseEnvironmentLights()
{
  _shading_program_environment_diffuse->pushUsage();
  glm::mat3 V_inv = glm::mat3(_camera.absoluteTransform());
  glUniformMatrix3fv(
    glGetUniformLocation(ShaderProgram::currentProgramId(), "V_inv"),
    1,
    GL_FALSE,
    &V_inv[0][0]);

  _geometry_fbo_quad->bindTextures();
  _sky_box->render();
  _geometry_fbo_quad->freeTextureUnits();
  _shading_program_environment_diffuse->popUsage();
}

void DeferredShadingRenderer::renderSkyBox()
{
  _cube_map_program->pushUsage();
  _geometry_fbo_quad->bindTextures();
    glDisable(GL_CULL_FACE);
  _sky_box->render();
  _geometry_fbo_quad->freeTextureUnits();
  glEnable(GL_CULL_FACE);
  _cube_map_program->popUsage();
}

void DeferredShadingRenderer::renderScreenSpaceReflections()
{
  _shading_program_reflections->pushUsage();
  glUniformMatrix4fv(
    glGetUniformLocation(ShaderProgram::currentProgramId(), "P_frag"), 1, GL_FALSE,
    &_camera.projectionTransform()[0][0]);

  glm::mat3 V_inv = glm::mat3(_camera.absoluteTransform());
  glUniformMatrix3fv(
    glGetUniformLocation(ShaderProgram::currentProgramId(), "V_inv"),
    1,
    GL_FALSE,
    &V_inv[0][0]);

  _light_fbo_quad->bindTextures();
  _geometry_fbo_quad->bindTextures();
  _sky_box->render();
  _light_fbo_quad->freeTextureUnits();
  _geometry_fbo_quad->freeTextureUnits();
  _shading_program_reflections->popUsage();
}

void DeferredShadingRenderer::renderIrradiance()
{
  _shading_program_irradiance->pushUsage();
  _light_fbo_quad->bindTextures();
  _light_fbo_quad->render();
  _light_fbo_quad->freeTextureUnits();
  _shading_program_irradiance->popUsage();
}


} }
