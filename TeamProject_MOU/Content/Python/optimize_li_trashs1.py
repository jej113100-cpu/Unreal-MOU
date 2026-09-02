"""
LI_Trashs1 최적화 스크립트 (검증된 최종 버전)

BP_Configuration_7 / _11 / _13(x5) / _14 액터들에 흩어져 있는
InstancedStaticMeshComponent(152개)를 고유 스태틱 메시 기준으로 묶어서,
새 액터 하나 아래에 HierarchicalInstancedStaticMeshComponent로 재구성한다.
Brush_0(빌더 브러시 잔재)도 함께 제거한다.

컴포넌트 생성 + 인스턴스 채우기 + 원본 삭제 + 저장을 전부 한 세션 안에서
끝낸다 (중간에 MCP로 저장했다가 다시 읽는 방식은 월드 파티션 External Actor가
디스크에 실제로 안 써져서 실패했었음 - 반드시 이 스크립트 하나로 끝까지 실행할 것).

실행 방법:
  py "Content/Python/optimize_li_trashs1.py"

TARGET_LEVEL을 바꿔서 복제본으로 먼저 검증한 뒤, 실제 자산에 적용하세요.
"""

import unreal

TARGET_LEVEL = "/Game/06_HES/_Level/_Level_Instance/LI_Trashs1"
NEW_ACTOR_LABEL = "SM_TrashCluster_HISM"
TARGET_CLASS_PREFIX = "BP_Configuration_"


def _hism_component_name_for_mesh(mesh):
    short = mesh.get_name()
    if short.startswith("SM_"):
        short = short[3:]
    return "HISM_" + short


def _collect_and_delete_sources():
    mesh_to_transforms = {}
    actors_to_delete = []

    for actor in unreal.EditorLevelLibrary.get_all_level_actors():
        cls_name = actor.get_class().get_name()
        is_config = cls_name.startswith(TARGET_CLASS_PREFIX)
        is_brush = isinstance(actor, unreal.Brush)

        if not (is_config or is_brush):
            continue

        if is_config:
            for comp in actor.get_components_by_class(unreal.InstancedStaticMeshComponent):
                mesh = comp.static_mesh
                if mesh is None:
                    continue
                count = comp.get_instance_count()
                for i in range(count):
                    world_tf = comp.get_instance_transform(i, world_space=True)
                    mesh_to_transforms.setdefault(mesh, []).append(world_tf)

        actors_to_delete.append(actor)

    return mesh_to_transforms, actors_to_delete


def _add_hism_component(actor, subsystem, root_handle, comp_name, mesh):
    params = unreal.AddNewSubobjectParams(
        parent_handle=root_handle,
        new_class=unreal.HierarchicalInstancedStaticMeshComponent,
        blueprint_context=None,
    )
    new_handle, fail_reason = subsystem.add_new_subobject(params)
    if str(fail_reason):
        raise RuntimeError(f"add_new_subobject failed for {comp_name}: {fail_reason}")

    subsystem.rename_subobject(new_handle, unreal.Text(comp_name))
    subobj_data = unreal.SubobjectDataBlueprintFunctionLibrary.get_data(new_handle)
    component = unreal.SubobjectDataBlueprintFunctionLibrary.get_associated_object(subobj_data)
    component.set_static_mesh(mesh)
    return component


def _build_hism_actor(mesh_to_transforms):
    new_actor = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.Actor, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0)
    )
    new_actor.set_actor_label(NEW_ACTOR_LABEL)

    subsystem = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    root_handle = subsystem.k2_gather_subobject_data_for_instance(new_actor)[0]

    for mesh, transforms in mesh_to_transforms.items():
        comp_name = _hism_component_name_for_mesh(mesh)
        component = _add_hism_component(new_actor, subsystem, root_handle, comp_name, mesh)
        for tf in transforms:
            component.add_instance(tf, True)

    return new_actor


def run():
    original_level = unreal.EditorLevelLibrary.get_editor_world().get_path_name().split(".")[0]

    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    unreal.EditorLoadingAndSavingUtils.load_map(TARGET_LEVEL)

    mesh_to_transforms, actors_to_delete = _collect_and_delete_sources()
    unreal.log(f"[optimize_li_trashs1] 고유 메시 {len(mesh_to_transforms)}종, "
               f"인스턴스 총 {sum(len(v) for v in mesh_to_transforms.values())}개, "
               f"삭제 대상 액터 {len(actors_to_delete)}개")

    new_actor = _build_hism_actor(mesh_to_transforms)

    for actor in actors_to_delete:
        unreal.EditorLevelLibrary.destroy_actor(actor)

    unreal.EditorLevelLibrary.save_current_level()
    unreal.log(f"[optimize_li_trashs1] 완료: {new_actor.get_actor_label()} 생성, "
               f"컴포넌트 {len(mesh_to_transforms)}개, 원본 액터 {len(actors_to_delete)}개 삭제")

    if original_level and original_level != TARGET_LEVEL:
        unreal.EditorLoadingAndSavingUtils.load_map(original_level)


run()
