# Regression Scenarios

The release test suite will contain twenty fixed scenarios:

- Six legal tasks that must complete.
- Five contract and policy rejections.
- Five timeout, cancellation, and navigation fault cases.
- Four recovery exhaustion and node restart cases.

Fast tests will use a fake `NavigateToPose` Action Server. A smaller subset
will run against the TurtleBot3/Nav2 simulation.
