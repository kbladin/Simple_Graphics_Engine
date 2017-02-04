#include <gl/glew.h>
#include <gl/glfw3.h>

#include <SGE/core/simple_graphics_engine.h>
#include <SGE/window/application_window_glfw.h>
#include <SGE/core/shader_manager.h>
#include <SGE/core/new_mesh.h>
#include "sge/core/create_mesh.h"
#include "sge/core/create_texture.h"
#include "sge/core/texture_unit.h"
#include "sge/core/frame_buffer_object.h"
#include "sge/core/render_buffer_object.h"
#include "sge/core/renderer.h"
#include "sge/core/deferred_shading_renderer.h"
#include "sge/object_extensions/renderable_model.h"
#include "sge/object_extensions/framebuffer_quad.h"
#include "sge/object_extensions/renderable_grid.h"
#include "sge/object_extensions/light_source.h"
#include "sge/core/debug_input.h"

#include <functional>
#include <memory>

using namespace sge::core;
using namespace sge::window;

class MyEngine : public SimpleGraphicsEngine
{
public:
  MyEngine();
  ~MyEngine();

  void update();
  DeferredShadingRenderer& renderer() { return _renderer; };
private:
  DeferredShadingRenderer _renderer;
  RenderableModel _monkey;
  PointLightSource _lamp;
  DirectionalLightSource _lamp2;
};

MyEngine::MyEngine() :
  SimpleGraphicsEngine(),
  _renderer(perspective_camera, 720 * 2, 480 * 2),
  _monkey("../../data/meshes/suzanne_highres.obj"),
  _lamp(glm::vec3(1.0,0.8,0.6), 1.5),
  _lamp2(glm::vec3(1.0,0.8,0.7), 0.15)
{
  _lamp.setTransform(glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 1.0f, 0.0f)));
  _lamp2.setTransform(glm::rotate(float(M_PI) * 0.15f, glm::vec3(-1.0f, 0.0f, -1.0f)));
  //_monkey.setTransform(glm::rotate(float(M_PI), glm::vec3(0.0f, 1.0f, 0.0f)));
  
  scene.addChild(_monkey);
  scene.addChild(camera());
  //camera().addChild(_lamp);
  scene.addChild(_lamp2);
}

MyEngine::~MyEngine()
{
  
}

void MyEngine::update()
{
  updateTransforms();

  _renderer.render(scene);
}


class DebugInputController : public Controller
{
public:
  DebugInputController(PerspectiveCamera& camera);
  ~DebugInputController(){};

  virtual void step(float dt) override;
private:
  PerspectiveCamera& _camera;
};

DebugInputController::DebugInputController(PerspectiveCamera& camera) :
  _camera(camera)
{
  DebugInput::value("roughness") = 1.0;
  DebugInput::value("IOR") = 2.0;
  DebugInput::value("FOV") = 45.0;
}

void DebugInputController::step(float dt)
{
  if (_keys_pressed.count(Key::KEY_O))
  {
    DebugInput::value("roughness") *= (1.0 - dt * 5.0);
    std::cout << "roughness = " << DebugInput::value("roughness") << std::endl;
  }
  if (_keys_pressed.count(Key::KEY_P))
  {
    DebugInput::value("roughness") *= (1.0 + dt * 5.0);
    std::cout << "roughness = " << DebugInput::value("roughness") << std::endl;
  }
  
  if (_keys_pressed.count(Key::KEY_K))
  {
    DebugInput::value("IOR") *= (1.0 - dt);
    std::cout << "IOR = " << DebugInput::value("IOR") << std::endl;
  }
  if (_keys_pressed.count(Key::KEY_L))
  {
    DebugInput::value("IOR") *= (1.0 + dt);
    std::cout << "IOR = " << DebugInput::value("IOR") << std::endl;
  }


  if (_keys_pressed.count(Key::KEY_N))
  {
    DebugInput::value("FOV") += 0.001;
  }
  if (_keys_pressed.count(Key::KEY_M))
  {
    DebugInput::value("FOV") -= 0.001;
  }
  
  DebugInput::value("roughness") = glm::clamp(DebugInput::value("roughness"), 0.0005f, 50.0f);
  DebugInput::value("IOR") = glm::clamp(DebugInput::value("IOR"), 1.1f, 4.0f);

  _camera.setFOV(DebugInput::value("FOV"));
}

int main(int argc, char const *argv[])
{
  ApplicationWindowGLFW window(720, 480);
  MyEngine e;
  SphericalController controller(e.camera());
  WindowSizeController window_controller(e.renderer());
  DebugInputController debug_controller(e.camera());
  window.addController(controller);
  window.addController(window_controller);
  window.addController(debug_controller);
  std::function<void(void)> loop = [&]()
  {
    e.update();
  };
  
  window.run(loop);

  return 0;
}
