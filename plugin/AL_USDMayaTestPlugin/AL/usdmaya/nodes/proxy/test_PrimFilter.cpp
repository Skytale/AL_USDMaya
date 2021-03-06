//
// Copyright 2017 Animal Logic
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.//
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
#include "test_usdmaya.h"
#include "AL/usdmaya/nodes/ProxyShape.h"
#include "AL/usdmaya/nodes/Transform.h"
#include "AL/usdmaya/nodes/Layer.h"
#include "AL/usdmaya/nodes/proxy/PrimFilter.h"
#include "AL/usdmaya/StageCache.h"
#include "maya/MFnTransform.h"
#include "maya/MSelectionList.h"
#include "maya/MGlobal.h"
#include "maya/MItDependencyNodes.h"
#include "maya/MDagModifier.h"
#include "maya/MFileIO.h"
#include "maya/MStringArray.h"

#include "pxr/usd/usd/stage.h"
#include "pxr/usd/sdf/types.h"
#include "pxr/usd/usd/attribute.h"
#include "pxr/usd/usdGeom/xform.h"
#include "pxr/usd/usdGeom/xformCommonAPI.h"

#include <fstream>

struct MockPrimFilterInterface : public AL::usdmaya::nodes::proxy::PrimFilterInterface
{
  SdfPathVector paths;

  TfToken getTypeForPath(const SdfPath& path) override
  {
    if(std::find(paths.cbegin(), paths.cend(), path) != paths.cend())
    {
      return TfToken("ALMayaReference");
    }
    return TfToken("");
  }

  bool getTypeInfo(TfToken type, bool& supportsUpdate, bool& requiresParent) override
  {
    supportsUpdate = true;
    requiresParent = true;
    return true;
  }
};

static const char* const g_removedPaths =
"#usda 1.0\n"
"\n"
"def ALMayaReference \"root\"\n"
"{\n"
"    asset mayaReference = \"/tmp/AL_usdmaya_test_cube.ma\"\n"
"    def ALMayaReference \"hip1\"\n"
"    {\n"
"        asset mayaReference = \"/tmp/AL_usdmaya_test_cube.ma\"\n"
"        def ALMayaReference \"knee1\"\n"
"        {\n"
"            asset mayaReference = \"/tmp/AL_usdmaya_test_cube.ma\"\n"
"            def ALMayaReference \"ankle1\"\n"
"            {\n"
"                asset mayaReference = \"/tmp/AL_usdmaya_test_cube.ma\"\n"
"                def ALMayaReference \"ltoe1\"\n"
"                {\n"
"                    asset mayaReference = \"/tmp/AL_usdmaya_test_cube.ma\"\n"
"                }\n"
"                def ALMayaReference \"rtoe1\"\n"
"                {\n"
"                    asset mayaReference = \"/tmp/AL_usdmaya_test_cube.ma\"\n"
"                }\n"
"            }\n"
"        }\n"
"    }\n"
"    def ALMayaReference \"hip2\"\n"
"    {\n"
"        asset mayaReference = \"/tmp/AL_usdmaya_test_cube.ma\"\n"
"        def ALMayaReference \"knee2\"\n"
"        {\n"
"            asset mayaReference = \"/tmp/AL_usdmaya_test_cube.ma\"\n"
"            def ALMayaReference \"ankle2\"\n"
"            {\n"
"                asset mayaReference = \"/tmp/AL_usdmaya_test_cube.ma\"\n"
"                def ALMayaReference \"ltoe2\"\n"
"                {\n"
"                    asset mayaReference = \"/tmp/AL_usdmaya_test_cube.ma\"\n"
"                }\n"
"                def ALMayaReference \"rtoe2\"\n"
"                {\n"
"                    asset mayaReference = \"/tmp/AL_usdmaya_test_cube.ma\"\n"
"                }\n"
"            }\n"
"        }\n"
"    }\n"
"}\n";


/// PrimFilter(const SdfPathVector& previousPrims, const std::vector<UsdPrim>& newPrimSet, nodes::ProxyShape* proxy);
TEST(PrimFilter, removedPaths)
{
  MFileIO::newFile(true);

  const std::string temp_path = "/tmp/AL_USDMayaTests_removedPaths.usda";

  // generate some data for the proxy shape
  {
    std::ofstream os(temp_path);
    os << g_removedPaths;
  }

  MString shapeName;

  MFnDagNode fn;
  MObject xform = fn.create("transform");
  MObject shape = fn.create("AL_usdmaya_ProxyShape", xform);
  shapeName = fn.name();

  AL::usdmaya::nodes::ProxyShape* proxy = (AL::usdmaya::nodes::ProxyShape*)fn.userNode();

  // force the stage to load
  proxy->filePathPlug().setString(temp_path.c_str());

  auto stage = proxy->getUsdStage();

  {
    // stage should be valid
    EXPECT_TRUE(stage);

    // should be composed of two layers
    SdfLayerHandle session = stage->GetSessionLayer();
    SdfLayerHandle root = stage->GetRootLayer();
    EXPECT_TRUE(session);
    EXPECT_TRUE(root);
  }

  MockPrimFilterInterface mockInterface;

  /// if nothing changes, the filter should give us the same list back as updatable prims
  {
    const SdfPathVector previous = {
      SdfPath("/root"),
      SdfPath("/root/hip1"),
      SdfPath("/root/hip1/knee1"),
      SdfPath("/root/hip1/knee1/ankle1"),
      SdfPath("/root/hip1/knee1/ankle1/ltoe1"),
      SdfPath("/root/hip1/knee1/ankle1/rtoe1"),
      SdfPath("/root/hip2"),
      SdfPath("/root/hip2/knee2"),
      SdfPath("/root/hip2/knee2/ankle2"),
      SdfPath("/root/hip2/knee2/ankle2/ltoe2"),
      SdfPath("/root/hip2/knee2/ankle2/rtoe2")
    };
    mockInterface.paths = previous;
    std::vector<UsdPrim> prims;
    for(auto it : previous)
    {
      prims.emplace_back(stage->GetPrimAtPath(it));
    }

    AL::usdmaya::nodes::proxy::PrimFilter filter(previous, prims, &mockInterface);

    EXPECT_TRUE(filter.removedPrimSet().empty());
    EXPECT_TRUE(filter.newPrimSet().empty());
    EXPECT_TRUE(filter.updatablePrimSet().size() == previous.size());
    EXPECT_TRUE(filter.transformsToCreate().empty());
  }

  /// if we aquire a few additional prims, those prims should remain in the newPrimSet (and transformToCreate set);
  /// the previous set should all appear in the
  {
    const SdfPathVector previous = {
      SdfPath("/root"),
      SdfPath("/root/hip1"),
      SdfPath("/root/hip1/knee1"),
      SdfPath("/root/hip1/knee1/ankle1"),
      SdfPath("/root/hip2"),
      SdfPath("/root/hip2/knee2"),
      SdfPath("/root/hip2/knee2/ankle2"),
    };
    mockInterface.paths = previous;
    std::vector<UsdPrim> prims;
    for(auto it : previous)
    {
      prims.emplace_back(stage->GetPrimAtPath(it));
    }
    prims.emplace_back(stage->GetPrimAtPath(SdfPath("/root/hip1/knee1/ankle1/ltoe1")));
    prims.emplace_back(stage->GetPrimAtPath(SdfPath("/root/hip1/knee1/ankle1/rtoe1")));
    prims.emplace_back(stage->GetPrimAtPath(SdfPath("/root/hip2/knee2/ankle2/ltoe2")));
    prims.emplace_back(stage->GetPrimAtPath(SdfPath("/root/hip2/knee2/ankle2/rtoe2")));

    AL::usdmaya::nodes::proxy::PrimFilter filter(previous, prims, &mockInterface);
    EXPECT_TRUE(filter.removedPrimSet().empty());
    EXPECT_TRUE(filter.newPrimSet().size() == 4);
    EXPECT_TRUE(filter.newPrimSet()[0].GetPath() == SdfPath("/root/hip1/knee1/ankle1/ltoe1"));
    EXPECT_TRUE(filter.newPrimSet()[1].GetPath() == SdfPath("/root/hip1/knee1/ankle1/rtoe1"));
    EXPECT_TRUE(filter.newPrimSet()[2].GetPath() == SdfPath("/root/hip2/knee2/ankle2/ltoe2"));
    EXPECT_TRUE(filter.newPrimSet()[3].GetPath() == SdfPath("/root/hip2/knee2/ankle2/rtoe2"));
    EXPECT_TRUE(filter.updatablePrimSet().size() == previous.size());
    EXPECT_EQ(4, filter.transformsToCreate().size());
    EXPECT_TRUE(filter.transformsToCreate()[0].GetPath() == SdfPath("/root/hip1/knee1/ankle1/ltoe1"));
    EXPECT_TRUE(filter.transformsToCreate()[1].GetPath() == SdfPath("/root/hip1/knee1/ankle1/rtoe1"));
    EXPECT_TRUE(filter.transformsToCreate()[2].GetPath() == SdfPath("/root/hip2/knee2/ankle2/ltoe2"));
    EXPECT_TRUE(filter.transformsToCreate()[3].GetPath() == SdfPath("/root/hip2/knee2/ankle2/rtoe2"));
  }

  /// Check to make sure that some prims are correctly removed.
  {
    SdfPathVector previous = {
      SdfPath("/root"),
      SdfPath("/root/hip1"),
      SdfPath("/root/hip1/knee1"),
      SdfPath("/root/hip1/knee1/ankle1"),
      SdfPath("/root/hip1/knee1/ankle1/ltoe1"),
      SdfPath("/root/hip1/knee1/ankle1/rtoe1"),
      SdfPath("/root/hip2"),
      SdfPath("/root/hip2/knee2"),
      SdfPath("/root/hip2/knee2/ankle2"),
      SdfPath("/root/hip2/knee2/ankle2/ltoe2"),
      SdfPath("/root/hip2/knee2/ankle2/rtoe2")
    };
    mockInterface.paths = previous;
    std::vector<UsdPrim> prims;
    for(auto it : previous)
    {
      prims.emplace_back(stage->GetPrimAtPath(it));
    }

    previous.emplace_back(SdfPath("/root/hip2/knee2/ankle2/rtoe3"));
    previous.emplace_back(SdfPath("/root/hip2/knee2/ankle2/rtoe4"));

    AL::usdmaya::nodes::proxy::PrimFilter filter(previous, prims, &mockInterface);
    EXPECT_TRUE(filter.removedPrimSet().size() == 2);
    EXPECT_TRUE(filter.removedPrimSet()[0] == SdfPath("/root/hip2/knee2/ankle2/rtoe4"));
    EXPECT_TRUE(filter.removedPrimSet()[1] == SdfPath("/root/hip2/knee2/ankle2/rtoe3"));
    EXPECT_TRUE(filter.newPrimSet().empty());
    EXPECT_TRUE(filter.updatablePrimSet().size() == (previous.size() - 2));
    EXPECT_TRUE(filter.transformsToCreate().empty());
  }
}
