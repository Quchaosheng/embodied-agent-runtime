"""
Checks the launch description without starting the stack.

This host has no vcan0, so the nodes cannot run here. What can be checked is
that the description builds, that every process the runtime needs is in it, and
that the values that must agree across processes are actually shared instead of
being repeated per node.
"""

import pathlib
import shutil
import tempfile
import unittest

from launch import LaunchContext
from launch.actions import DeclareLaunchArgument, OpaqueFunction
from launch_ros.actions import Node
from launch_ros.utilities.evaluate_parameters import evaluate_parameter_dict
import yaml

PACKAGE = pathlib.Path(__file__).resolve().parents[1]
LAUNCH = PACKAGE / 'launch' / 'runtime.launch.py'

# (package, executable) for every process that has to be running for
# ExecuteWorkflow to reach the device.
CORE = {
    ('device_bridge', 'device_bridge_node'),
    ('task_executor', 'task_executor_node'),
    ('runtime_monitor', 'runtime_monitor_node'),
    ('runtime_history', 'runtime_history_node'),
    ('task_orchestrator', 'task_orchestrator_node'),
    ('runtime_gateway', 'runtime_gateway_node'),
}


def load_launch():
    """Import the launch file as a module, the way ros2 launch does."""
    import importlib.util

    spec = importlib.util.spec_from_file_location('runtime_launch_under_test', LAUNCH)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def render(module, **overrides):
    """Return the Node actions the description produces for these arguments."""
    description = module.generate_launch_description()
    context = LaunchContext()
    context.launch_configurations.update(overrides)
    actions = []
    for entity in description.entities:
        if isinstance(entity, DeclareLaunchArgument):
            # Normally done by the launch service: visiting the argument is
            # what puts its default into the context.
            entity.visit(context)
            continue
        if isinstance(entity, OpaqueFunction):
            actions.extend(entity.visit(context))
        else:
            actions.append(entity)
    return [action for action in actions if isinstance(action, Node)]


def parameters_of(node):
    """
    Flatten a Node's parameter list into one dict, last value winning.

    Entries are a parameter dict or a params file; launch_ros owns both
    representations, so its own evaluation is used instead of re-deriving the
    substitution rules here.
    """
    context = LaunchContext()
    merged = {}
    for entry in node._Node__parameters:
        if hasattr(entry, 'evaluate'):
            # ParameterFile
            with open(entry.evaluate(context), encoding='utf-8') as handle:
                document = yaml.safe_load(handle)
            for values in document.values():
                merged.update(values.get('ros__parameters', {}))
        else:
            merged.update(evaluate_parameter_dict(context, entry))
    return merged


class RuntimeLaunchTest(unittest.TestCase):

    def setUp(self):
        self.module = load_launch()
        self.directory = pathlib.Path(tempfile.mkdtemp(prefix='runtime_bringup_test_'))
        # Every case overrides both paths: the checked-in defaults live under
        # /var/tmp, which is not writable in a sandboxed test run, and a test
        # that fails on its own environment proves nothing about the launch file.
        self.arguments = {
            'database_path': str(self.directory / 'history.sqlite3'),
            'stage_event_log_path': str(self.directory / 'stage.jsonl'),
        }
        self.nodes = self.render()

    def tearDown(self):
        shutil.rmtree(self.directory, ignore_errors=True)

    def render(self, **overrides):
        return render(self.module, **{**self.arguments, **overrides})

    def test_starts_every_core_process_exactly_once(self):
        started = [(node.node_package, node.node_executable) for node in self.nodes]
        self.assertEqual(len(started), len(set(started)), 'a process is started twice')
        self.assertEqual(set(started) - {('virtual_can_device', 'virtual_can_device_node')},
                         CORE)

    def test_database_and_stage_log_are_shared_not_repeated(self):
        by_package = {node.node_package: parameters_of(node) for node in self.nodes}
        self.assertEqual(by_package['runtime_history']['database_path'],
                         self.arguments['database_path'])
        self.assertEqual(by_package['runtime_gateway']['database_path'],
                         self.arguments['database_path'])
        for package in ('device_bridge', 'task_executor', 'runtime_gateway'):
            self.assertEqual(by_package[package]['stage_event_log_path'],
                             self.arguments['stage_event_log_path'], package)

    def test_can_interface_override_reaches_both_ends_of_the_bus(self):
        # The params files also set vcan0; overriding proves the launch argument
        # wins on both sides, which is what stops a bridge and an ECU from
        # sitting on different interfaces and looking merely idle.
        by_package = {
            node.node_package: parameters_of(node)
            for node in self.render(can_interface='can1')
        }
        self.assertEqual(by_package['device_bridge']['interface_name'], 'can1')
        self.assertEqual(by_package['virtual_can_device']['interface_name'], 'can1')

    def test_creates_the_directory_the_database_needs(self):
        # SQLite will not create a directory. Point both paths at a directory
        # that does not exist yet and check the description makes it.
        nested = self.directory / 'does' / 'not' / 'exist'
        self.render(database_path=str(nested / 'history.sqlite3'))
        self.assertTrue(nested.is_dir())

    def test_virtual_device_can_be_left_out(self):
        self.assertNotIn(
            'virtual_can_device',
            {node.node_package for node in self.render(use_virtual_device='false')},
        )

    def test_respawn_stays_off_unless_asked_for(self):
        # ExecuteLocal keeps it as a mangled attribute; there is no accessor.
        self.assertFalse(any(node._ExecuteLocal__respawn for node in self.nodes))


if __name__ == '__main__':
    unittest.main()
