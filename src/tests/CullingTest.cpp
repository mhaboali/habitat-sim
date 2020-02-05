// Copyright (c) Facebook, Inc. and its affiliates.
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include <Corrade/Containers/Optional.h>
#include <Corrade/Utility/Directory.h>
#include <Magnum/EigenIntegration/Integration.h>
#include <Magnum/GL/SampleQuery.h>
#include <Magnum/Math/Frustum.h>
#include <Magnum/Math/Intersection.h>
#include <Magnum/Math/Range.h>
#include <gtest/gtest.h>
#include <string>

#include "esp/assets/ResourceManager.h"
#include "esp/gfx/RenderCamera.h"
#include "esp/gfx/RenderTarget.h"
#include "esp/gfx/WindowlessContext.h"
#include "esp/scene/SceneManager.h"

#include "configure.h"

namespace Cr = Corrade;
namespace Mn = Magnum;

using esp::assets::ResourceManager;
using esp::scene::SceneManager;

TEST(CullingTest, computeAbsoluteAABB) {
  // must create a GL context which will be used in the resource manager
  esp::gfx::WindowlessContext::uptr context_ =
      esp::gfx::WindowlessContext::create_unique(0);

  // must declare these in this order due to avoid deallocation errors
  ResourceManager resourceManager;
  SceneManager sceneManager;

  std::string sceneFile =
      Cr::Utility::Directory::join(TEST_ASSETS, "objects/5boxes.glb");

  int sceneID = sceneManager.initSceneGraph();
  auto& sceneGraph = sceneManager.getSceneGraph(sceneID);
  esp::scene::SceneNode& sceneRootNode = sceneGraph.getRootNode();
  auto& drawables = sceneGraph.getDrawables();
  const esp::assets::AssetInfo info =
      esp::assets::AssetInfo::fromPath(sceneFile);
  bool loadSuccess =
      resourceManager.loadScene(info, &sceneRootNode, &drawables);
  EXPECT_EQ(loadSuccess, true);

  std::vector<Mn::Range3D> aabbs;
  for (unsigned int iDrawable = 0; iDrawable < drawables.size(); ++iDrawable) {
    Cr::Containers::Optional<Mn::Range3D> aabb =
        dynamic_cast<esp::scene::SceneNode&>(drawables[iDrawable].object())
            .getAbsoluteAABB();
    if (aabb) {
      aabbs.emplace_back(*aabb);
    }
  }

  /* ground truth
   *
   * Objects: (TODO: add more objects to the test, e.g., sphere, cylinder)
   *  a) a cube, with edge length 2.0
   *
   */
  std::vector<Mn::Range3D> aabbsGroundTruth;
  // Box 0: root (parent: null), object "a", centered at origin
  aabbsGroundTruth.emplace_back(Mn::Vector3{-1.0, -1.0, -1.0},
                                Mn::Vector3{1.0, 1.0, 1.0});
  // Box 1: (parent, Box 0), object "a", relative translation (0.0, -4.0, 0.0)
  aabbsGroundTruth.emplace_back(Mn::Vector3{-1.0, -5.0, -1.0},
                                Mn::Vector3{1.0, -3.0, 1.0});
  // Box 2: (parent, Box 1), object "a", relative translation (0.0, 0.0, 4.0)
  aabbsGroundTruth.emplace_back(Mn::Vector3{-1.0, -5.0, 3.0},
                                Mn::Vector3{1.0, -3.0, 5.0});
  // Box 3: (parent, Box 0), object "a", relative translation (-4.0, 0.0, 4.0),
  // relative rotation pi/4 (ccw) around local z-axis of Box 3
  aabbsGroundTruth.emplace_back(
      Mn::Vector3{-4.0f - sqrt(2.0f), -sqrt(2.0f), 3.0},
      Mn::Vector3{-4.0f + sqrt(2.0f), sqrt(2.0f), 5.0});
  // Box 4: (parent, Box 3), object "a", relative translation (8.0, 0.0, 0.0),
  // relative rotation pi/4 (ccw) around local z-axis of Box 4
  aabbsGroundTruth.emplace_back(Mn::Vector3{3.0, -1.0, 3.0},
                                Mn::Vector3{5.0, 1.0, 5.0});

  // compare against the ground truth
  EXPECT_EQ(aabbs.size(), aabbsGroundTruth.size());
  const float epsilon = 1e-6;
  for (unsigned int iBox = 0; iBox < aabbsGroundTruth.size(); ++iBox) {
    CHECK_LE(std::abs((aabbs[iBox].min() - aabbsGroundTruth[iBox].min()).dot()),
             epsilon);
    CHECK_LE(std::abs((aabbs[iBox].max() - aabbsGroundTruth[iBox].max()).dot()),
             epsilon);
  }
}

TEST(CullingTest, frustumCulling) {
  // must create a GL context which will be used in the resource manager
  esp::gfx::WindowlessContext::uptr context_ =
      esp::gfx::WindowlessContext::create_unique(0);

  // must declare these in this order due to avoid deallocation errors
  ResourceManager resourceManager;
  SceneManager sceneManager;

  std::string sceneFile =
      Cr::Utility::Directory::join(TEST_ASSETS, "objects/5boxes.glb");

  // load the scene
  int sceneID = sceneManager.initSceneGraph();
  auto& sceneGraph = sceneManager.getSceneGraph(sceneID);
  esp::scene::SceneNode& sceneRootNode = sceneGraph.getRootNode();
  auto& drawables = sceneGraph.getDrawables();
  const esp::assets::AssetInfo info =
      esp::assets::AssetInfo::fromPath(sceneFile);
  bool loadSuccess =
      resourceManager.loadScene(info, &sceneRootNode, &drawables);
  EXPECT_EQ(loadSuccess, true);

  // set the camera
  esp::gfx::RenderCamera& renderCamera = sceneGraph.getDefaultRenderCamera();

  // The camera to be set:
  // pos: {7.3589f, -6.9258f,4.9583f}
  // rotation: 77.4 deg, around {0.773, 0.334, 0.539}
  // fov = 39.6 deg
  // resolution: 800 x 600
  // clip planes (near: 0.1m, far: 100m)
  // with such a camera, the box 3 should be invisible, box 0, 1, 2, 4 should be
  // visible.

  // NOTE: the following test results have been visually verified in utility
  // viewer
  Mn::Vector2i frameBufferSize{800, 600};
  renderCamera.setProjectionMatrix(frameBufferSize.x(),  // width
                                   frameBufferSize.y(),  // height
                                   0.01f,                // znear
                                   100.0f,               // zfar
                                   39.6f);               // hfov

  esp::scene::SceneNode agentNode = sceneGraph.getRootNode().createChild();
  esp::scene::SceneNode cameraNode = agentNode.createChild();
  cameraNode.translate({7.3589f, -6.9258f, 4.9583f});
  const Mn::Vector3 axis{0.773, 0.334, 0.539};
  cameraNode.rotate(Mn::Math::Deg<float>(77.4f), axis.normalized());
  renderCamera.node().setTransformation(cameraNode.absoluteTransformation());

  // collect all the drawables and their transformations
  std::vector<std::pair<std::reference_wrapper<Mn::SceneGraph::Drawable3D>,
                        Mn::Matrix4>>
      drawableTransforms = renderCamera.drawableTransformations(drawables);

  // do the culling (to create the testing group)
  size_t numVisibles = renderCamera.cull(drawableTransforms);
  auto newEndIter = drawableTransforms.begin() + numVisibles;

  // create a render target
  Mn::Matrix4 projMtx = renderCamera.projectionMatrix();
  esp::gfx::RenderTarget::uptr target = esp::gfx::RenderTarget::create_unique(
      frameBufferSize, esp::gfx::calculateDepthUnprojection(projMtx));

  // ============== Test 1 ==================
  // draw all the invisibles reported by cull()
  // check passed if 0 sample is returned (that means they are indeed
  // invisibles.)
  {
    // objects will contain all the invisible ones
    std::vector<std::pair<std::reference_wrapper<Mn::SceneGraph::Drawable3D>,
                          Mn::Matrix4>>
        objects = renderCamera.drawableTransformations(drawables);

    // CAREFUL:
    // all the invisible ones are NOT stored at [newEndIter,
    // drawableTransforms.end()]. This is because std::remove_if will only move
    // elements, not swap elements
    objects.erase(
        std::remove_if(
            objects.begin(), objects.end(),
            [&](const std::pair<
                std::reference_wrapper<Mn::SceneGraph::Drawable3D>,
                Mn::Matrix4>& a) {
              for (std::vector<std::pair<
                       std::reference_wrapper<Mn::SceneGraph::Drawable3D>,
                       Mn::Matrix4>>::iterator iter =
                       drawableTransforms.begin();
                   iter != newEndIter; ++iter) {
                if (std::addressof(a.first.get()) ==
                    std::addressof(iter->first.get())) {
                  return true;  // it is visible, remove it
                }
              }
              return false;
            }),
        objects.end());

    target->renderEnter();
    Mn::GL::SampleQuery q{Mn::GL::SampleQuery::Target::AnySamplesPassed};
    q.begin();
    renderCamera.MagnumCamera::draw(objects);
    q.end();
    target->renderExit();

    EXPECT_EQ(q.result<bool>(), false);
  }

  // ============== Test 2 ==================
  // draw the visibles one by one.
  // check if each one is a genuine visible drawable
  unsigned int numVisibleObjectsGroundTruth = 0;
  auto renderOneDrawable =
      [&](const std::pair<std::reference_wrapper<Mn::SceneGraph::Drawable3D>,
                          Mn::Matrix4>& a) {
        std::vector<std::pair<
            std::reference_wrapper<Mn::SceneGraph::Drawable3D>, Mn::Matrix4>>
            objects;
        objects.emplace_back(a);

        target->renderEnter();
        Mn::GL::SampleQuery q{Mn::GL::SampleQuery::Target::AnySamplesPassed};
        q.begin();
        renderCamera.MagnumCamera::draw(objects);
        q.end();
        target->renderExit();

        // check if it a genuine visible drawable
        EXPECT_EQ(q.result<bool>(), true);

        if (q.result<bool>()) {
          numVisibleObjectsGroundTruth++;
        }
      };
  for_each(drawableTransforms.begin(), newEndIter, renderOneDrawable);

  // ============== Test 3 ==================
  // draw using the RenderCamera overload draw()
  target->renderEnter();
  size_t numVisibleObjects =
      renderCamera.draw(drawables, true /* enable frustum culling */);
  target->renderExit();
  EXPECT_EQ(numVisibleObjects, numVisibleObjectsGroundTruth);
}
