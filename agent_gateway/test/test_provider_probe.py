from agent_gateway.provider_probe import main


def test_provider_probe_checks_fake_provider_without_ros(capsys) -> None:
    return_code = main(["--provider", "fake", "回充电桩"])

    captured = capsys.readouterr()
    assert return_code == 0
    assert "target=dock" in captured.out
    assert "No ROS Action was sent." in captured.out
