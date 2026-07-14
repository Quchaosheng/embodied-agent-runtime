# Simulation

This directory will contain the reproducible TurtleBot3/Nav2 launch setup,
map, keepout configuration, and target poses used by integration tests.

The first simulation scope is intentionally limited to `dock`, `workbench`,
`home`, and one keepout zone. SLAM, vision, manipulation, and multi-robot
scenarios are out of scope for the first release.

`config/targets.yaml` is the single source of truth for named runtime poses.
The task_executor package installs this file into its share directory and
fails to start if a contract target is missing, unknown, non-finite, outside
the map frame, or contains an unreviewed pose field. The current numbers remain
placeholders until the first TurtleBot3 map is selected and inspected.
