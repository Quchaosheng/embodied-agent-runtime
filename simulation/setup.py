from glob import glob

from setuptools import find_packages, setup

package_name = "runtime_simulation"

setup(
    name=package_name,
    version="0.1.0",
    packages=find_packages(exclude=["test"]),
    data_files=[
        ("share/ament_index/resource_index/packages", ["resource/" + package_name]),
        ("share/" + package_name, ["package.xml", "README.md"]),
        ("share/" + package_name + "/config", glob("config/*.yaml")),
        ("share/" + package_name + "/launch", glob("launch/*.launch.py")),
        ("share/" + package_name + "/rviz", glob("rviz/*.rviz")),
    ],
    install_requires=["setuptools", "PyYAML"],
    zip_safe=True,
    maintainer="Quchaosheng",
    maintainer_email="quchaosheng000406@163.com",
    description="TurtleBot3, Gazebo, and Nav2 system simulation for the embodied runtime.",
    license="Apache-2.0",
    tests_require=["pytest"],
    entry_points={
        "console_scripts": [
            "initial_pose_publisher = runtime_simulation.initial_pose:main",
            "scene_marker_publisher = runtime_simulation.scene_markers:main",
            "validate_map_targets = runtime_simulation.validate_map:main",
        ],
    },
)
