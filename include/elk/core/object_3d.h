#pragma once

#include <vector>
  
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/transform.hpp>

namespace elk { namespace core {

class Renderable;
class PointLightSource;
class DirectionalLightSource;
class Renderer;
class PerspectiveCamera;

//! An object positioned in 3D space.
/*!
  This object can not be rendered by itself. It needs a child or _children in
  form of meshes (TriangleMesh or LineMesh).
  All objects inheriting from Object3D can be added as child.
*/
class Object3D {
public:
  Object3D() {};
  //! Destructor
  /*!
    The _children of the Object3D is not destroyed when the Object3D is destroyed.
    The _children needs to be destroyed explicitly.
  */
  virtual ~Object3D() {};

  //! Adds a child node
  void addChild(Object3D& child);
  //! Removes a child from the list of pointers
  /*!
    The call is recursive so if the parameter \param child is found among
    _children of _children it is also removed
  */
  void removeChild(Object3D& child);
  void updateTransform(const glm::mat4& stacked_transform);
  virtual void submit(Renderer& renderer);
  virtual void update(double dt);

  const glm::mat4& relativeTransform() const;
  const glm::mat4& absoluteTransform() const;
  void setTransform(const glm::mat4& transform);
private:
  std::vector<Object3D*> _children;
  glm::mat4 _relative_transform;
  glm::mat4 _absolute_transform;
};

// Data needed when rendering
struct UsefulRenderData
{
  const PerspectiveCamera& camera;
};

class RenderableDeferred : public Object3D
{
public:
  RenderableDeferred() : Object3D() {};
  ~RenderableDeferred() {};
  virtual void submit(Renderer& renderer) override;
  virtual void render(const UsefulRenderData& render_data) = 0;
};

class RenderableForward : public Object3D
{
public:
  RenderableForward() : Object3D() {};
  ~RenderableForward() {};
  virtual void submit(Renderer& renderer) override;
  virtual void render(const UsefulRenderData& render_data) = 0;
};

} }
