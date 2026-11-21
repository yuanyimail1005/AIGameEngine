#!/usr/bin/env python3
"""(Re)builds the feature test project (TestProject/) entirely through the editor's AI control interface.

This doubles as an end-to-end example of how an AI agent builds a scene. Binary assets come from
generate_test_assets.py; scripts live in TestProject/Assets/Scripts.

Usage: python3 Tools/build_test_project.py [--screenshot docs/images/feature_test.png]
"""

import argparse
import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(__file__))
from ember_ai import EmberEditor, REPO_ROOT  # noqa: E402

PROJECT_DIR = os.path.join(REPO_ROOT, "TestProject")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--screenshot", help="save a rendered image of the scene")
    args = parser.parse_args()

    # Fresh project file and scenes; generated assets and scripts are kept.
    for stale in ("FeatureTest.eproject",):
        path = os.path.join(PROJECT_DIR, stale)
        if os.path.exists(path):
            os.remove(path)
    shutil.rmtree(os.path.join(PROJECT_DIR, "Assets", "Scenes"), ignore_errors=True)
    shutil.rmtree(os.path.join(PROJECT_DIR, "Assets", "Prefabs"), ignore_errors=True)
    shutil.rmtree(os.path.join(PROJECT_DIR, "Assets", "Materials"), ignore_errors=True)

    with EmberEditor.launch() as editor:
        editor.call("new_project", path=PROJECT_DIR, name="FeatureTest")
        editor.call("delete_asset", path="Scenes/Main.escene")
        editor.call("new_scene", name="FeatureTest")

        def create(name, parent=None, **components):
            params = {"name": name, "components": components}
            if parent:
                params["parent"] = parent
            return editor.call("create_entity", **params)

        # Camera, lights, sky
        create("Main Camera", Camera={"PerspectiveFOVDegrees": 50},
               Transform={"Translation": [0, 7, 15], "RotationDegrees": [-22, 0, 0]}, AudioListener={})
        create("Sun", DirectionalLight={"Intensity": 3.5, "Color": [1.0, 0.95, 0.85], "ShadowSoftness": 1.0},
               Transform={"RotationDegrees": [-50, 35, 0]})
        create("Sky", SkyLight={"EnvironmentMap": "HDRI/sky.hdr", "Intensity": 1.0})

        # Materials and textures
        editor.call("create_material", path="Materials/Gold.ematerial",
                    values={"BaseColor": [1.0, 0.78, 0.34], "Metallic": 1.0, "Roughness": 0.25})

        # Static ground (collider without a rigid body = static), textured via a material override
        create("Ground", Transform={"Translation": [0, -0.5, 0], "Scale": [30, 1, 30]},
               MeshRenderer={"Mesh": "builtin://Cube", "MaterialOverride": {"BaseColorTexture": "Textures/checker.png",
                                                                          "Roughness": 0.85, "UVTiling": [8, 8]}},
               BoxCollider={})

        # Dynamic bodies
        create("Ball", Transform={"Translation": [-3, 5, 0]},
               MeshRenderer={"Mesh": "builtin://Sphere", "MaterialOverride": {"BaseColor": [0.85, 0.15, 0.1],
                                                                            "Metallic": 0.1, "Roughness": 0.35}},
               RigidBody={"Type": "Dynamic", "Restitution": 0.5}, SphereCollider={}, Script={"Script": "Scripts/Ball.lua"})
        create("Faller", Transform={"Translation": [3, 8, 0], "Scale": [0.8, 0.8, 0.8]},
               MeshRenderer={"Mesh": "builtin://Cube", "MaterialOverride": {"BaseColor": [0.2, 0.7, 0.3]}},
               RigidBody={"Type": "Dynamic"}, BoxCollider={})
        create("Pusher", Transform={"Translation": [9, 2, -2], "Scale": [0.5, 0.5, 0.5]},
               MeshRenderer={"Mesh": "builtin://Cube", "MaterialOverride": {"BaseColor": [0.9, 0.9, 0.2]}},
               RigidBody={"Type": "Dynamic", "GravityScale": 0.0, "LinearDamping": 0.0, "AngularDamping": 0.0}, BoxCollider={})

        # Trigger volume (transparent)
        create("TriggerZone", Transform={"Translation": [3, 4, 0], "Scale": [3, 1, 3]},
               MeshRenderer={"Mesh": "builtin://Cube", "CastShadows": False,
                             "MaterialOverride": {"BaseColor": [0.2, 0.5, 1.0, 0.35], "AlphaMode": "Blend"}},
               RigidBody={"Type": "Static", "IsTrigger": True}, BoxCollider={}, Script={"Script": "Scripts/TriggerZone.lua"})

        # glTF model, material asset, kinematic mover, spinner
        create("Crate", Transform={"Translation": [0, 0.5, -3]}, MeshRenderer={"Mesh": "Models/Crate.gltf"}, BoxCollider={})
        create("GoldStatue", Transform={"Translation": [-5, 1, -4], "Scale": [1.5, 1.5, 1.5]},
               MeshRenderer={"Mesh": "builtin://Sphere", "Material": "Materials/Gold.ematerial"})
        create("Mover", Transform={"Translation": [-6, 1, 3]},
               MeshRenderer={"Mesh": "builtin://Capsule", "MaterialOverride": {"BaseColor": [0.6, 0.3, 0.8]}},
               RigidBody={"Type": "Kinematic"}, CapsuleCollider={}, Script={"Script": "Scripts/Mover.lua"})
        create("Spinner", Transform={"Translation": [5, 1, 3]},
               MeshRenderer={"Mesh": "builtin://Cylinder", "MaterialOverride": {"BaseColor": [0.9, 0.5, 0.1], "Metallic": 0.6}},
               Script={"Script": "Scripts/Spinner.lua"})

        # Local lights (with hierarchy: the bulb is a child of the lamp)
        lamp = create("Lamp", Transform={"Translation": [-5, 2.5, -1]},
                      PointLight={"Color": [1.0, 0.6, 0.25], "Intensity": 6.0, "Range": 8.0})
        create("LampBulb", parent=lamp["id"], Transform={"Scale": [0.25, 0.25, 0.25]},
               MeshRenderer={"Mesh": "builtin://Sphere", "CastShadows": False,
                             "MaterialOverride": {"BaseColor": [0, 0, 0], "EmissiveColor": [1.0, 0.6, 0.25], "EmissiveStrength": 4.0}})
        create("Spotlight", Transform={"Translation": [5, 5, -4], "RotationDegrees": [-90, 0, 0]},
               SpotLight={"Color": [0.4, 0.6, 1.0], "Intensity": 25.0, "Range": 10.0, "InnerConeAngle": 0.3, "OuterConeAngle": 0.5})

        # Audio
        create("Speaker", Transform={"Translation": [0, 1, 0]},
               AudioSource={"Clip": "Audio/hum.wav", "Loop": True, "PlayOnStart": True, "Volume": 0.3})

        # Prefab: a scripted, glowing projectile
        projectile = create("Projectile", Transform={"Scale": [0.3, 0.3, 0.3]},
                            MeshRenderer={"Mesh": "builtin://Sphere", "CastShadows": False,
                                          "MaterialOverride": {"BaseColor": [0, 0, 0], "EmissiveColor": [0.3, 1.0, 0.4],
                                                               "EmissiveStrength": 3.0}},
                            Script={"Script": "Scripts/Projectile.lua"})
        editor.call("create_prefab", entity=projectile["id"], path="Prefabs/Projectile.eprefab")
        editor.call("destroy_entity", entity=projectile["id"])

        # Test controller
        create("TestController", Script={"Script": "Scripts/FeatureTest.lua"})

        editor.call("save_scene", path="Scenes/FeatureTest.escene")
        editor.call("set_project", StartScene="Scenes/FeatureTest.escene", WindowWidth=1280, WindowHeight=720)

        if args.screenshot:
            editor.call("run_frames", frames=90)
            result = editor.call("capture", path=os.path.abspath(args.screenshot), width=1280, height=720)
            editor.call("stop")
            print("Screenshot:", result["path"])

        state = editor.call("get_info")
        print(f"Built {state['scene_path']} with {state['entity_count']} entities")


if __name__ == "__main__":
    main()
