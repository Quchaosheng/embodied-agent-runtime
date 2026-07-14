import math

import pytest

from runtime_simulation.initial_pose import yaw_quaternion


def test_yaw_quaternion_is_normalized():
    z, w = yaw_quaternion(math.pi / 2.0)

    assert z == pytest.approx(math.sqrt(0.5))
    assert w == pytest.approx(math.sqrt(0.5))
    assert z * z + w * w == pytest.approx(1.0)


def test_yaw_quaternion_rejects_nonfinite_input():
    with pytest.raises(ValueError, match="finite"):
        yaw_quaternion(float("nan"))
