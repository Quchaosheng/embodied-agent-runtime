from setuptools import find_packages, setup

package_name = "agent_gateway"

setup(
    name=package_name,
    version="0.1.0",
    packages=find_packages(exclude=["test"]),
    data_files=[
        ("share/ament_index/resource_index/packages", ["resource/" + package_name]),
        ("share/" + package_name, ["package.xml", "README.md"]),
        ("share/" + package_name + "/config", ["config/provider.example.env"]),
        ("share/" + package_name + "/schema", ["schema/mission_plan.schema.json"]),
        (
            "share/" + package_name + "/evaluation",
            ["evaluation/intent_cases.json"],
        ),
    ],
    install_requires=["setuptools", "jsonschema"],
    zip_safe=True,
    maintainer="Quchaosheng",
    maintainer_email="quchaosheng000406@163.com",
    description="Strict model-output normalization for Embodied Agent Runtime.",
    license="Apache-2.0",
    tests_require=["pytest"],
    entry_points={
        "console_scripts": [
            "ask = agent_gateway.cli:main",
            "evaluate_intents = agent_gateway.evaluation_cli:main",
            "probe_provider = agent_gateway.provider_probe:main",
        ],
    },
)
