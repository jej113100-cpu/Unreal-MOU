"""
LI_Trashs1_TEST(복제본)에서만 돌리는 진단 스크립트.
SubobjectDataSubsystem으로 액터에 HISM 컴포넌트를 새로 추가하고,
인스턴스 하나를 넣은 뒤, 한 세션 안에서 저장까지 하고 나서
리로드해도 살아남는지 확인한다.

실행: py "Content/Python/test_hism_probe.py"
"""

import unreal

TEST_LEVEL = "/Game/06_HES/_Level/_Level_Instance/LI_Trashs1_TEST"
MESH_PATH = "/Game/Scene_Junkyard/Assets/MS/3D/Res_Jun_Wheel_Metal_Weathered_01/SM_Res_Jun_Wheel_Metal_Weathered_01.SM_Res_Jun_Wheel_Metal_Weathered_01"
ACTOR_LABEL = "HISM_Probe_Actor"


def run():
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    unreal.EditorLoadingAndSavingUtils.load_map(TEST_LEVEL)

    new_actor = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.Actor, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0)
    )
    new_actor.set_actor_label(ACTOR_LABEL)
    unreal.log(f"[probe] actor spawned: {new_actor.get_name()}")

    subsystem = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    root_handles = subsystem.k2_gather_subobject_data_for_instance(new_actor)
    unreal.log(f"[probe] root handle count: {len(root_handles)}")

    params = unreal.AddNewSubobjectParams(
        parent_handle=root_handles[0],
        new_class=unreal.HierarchicalInstancedStaticMeshComponent,
        blueprint_context=None,
    )
    new_handle, fail_reason = subsystem.add_new_subobject(params)
    unreal.log(f"[probe] add_new_subobject fail_reason: '{fail_reason}'")

    subobj_data = unreal.SubobjectDataBlueprintFunctionLibrary.get_data(new_handle)
    try:
        component = unreal.SubobjectDataBlueprintFunctionLibrary.get_associated_object(subobj_data)
    except AttributeError as e:
        unreal.log_error(f"[probe] get_object via library failed: {e}")
        unreal.log_error(f"[probe] SubobjectData dir(): {[a for a in dir(subobj_data) if not a.startswith('_')]}")
        unreal.log_error(f"[probe] SubobjectDataBlueprintFunctionLibrary dir(): "
                          f"{[a for a in dir(unreal.SubobjectDataBlueprintFunctionLibrary) if not a.startswith('_')]}")
        raise
    unreal.log(f"[probe] component created: {component} (class={component.get_class().get_name()})")

    subsystem.rename_subobject(new_handle, unreal.Text("HISM_Probe"))

    mesh = unreal.load_asset(MESH_PATH)
    component.set_static_mesh(mesh)
    component.add_instance(unreal.Transform(unreal.Vector(100, 0, 0)), True)
    unreal.log(f"[probe] instance count after add: {component.get_instance_count()}")

    unreal.EditorLevelLibrary.save_current_level()
    unreal.log("[probe] saved. reloading to verify persistence...")

    unreal.EditorLoadingAndSavingUtils.load_map(TEST_LEVEL)

    found = None
    for a in unreal.EditorLevelLibrary.get_all_level_actors():
        if a.get_actor_label() == ACTOR_LABEL:
            found = a
            break

    if found is None:
        unreal.log_error("[probe] RESULT: FAIL - actor did not survive reload")
        return

    comps = found.get_components_by_class(unreal.HierarchicalInstancedStaticMeshComponent)
    if not comps:
        unreal.log_error("[probe] RESULT: FAIL - actor survived but component did not")
        return

    count = comps[0].get_instance_count()
    unreal.log(f"[probe] RESULT: {'PASS' if count == 1 else 'FAIL'} - instance_count={count} after reload")


run()
