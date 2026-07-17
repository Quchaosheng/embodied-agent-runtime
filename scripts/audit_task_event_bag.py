#!/usr/bin/env python3

import argparse
from pathlib import Path
import sys


TASK_EVENT_TOPIC = "/task_events"


def format_event(event: object) -> str:
    detail = " ".join(event.detail.split())
    return (
        f"task_id={event.task_id} state={event.state} "
        f"error_code={event.error_code} attempt={event.attempt} detail={detail}"
    )


def read_task_events(bag_path: Path) -> list[object]:
    if not bag_path.is_dir():
        raise RuntimeError(f"bag directory does not exist: {bag_path}")

    import rosbag2_py
    from rclpy.serialization import deserialize_message
    from rosidl_runtime_py.utilities import get_message

    reader = rosbag2_py.SequentialReader()
    try:
        reader.open(
            rosbag2_py.StorageOptions(uri=str(bag_path), storage_id="mcap"),
            rosbag2_py.ConverterOptions(
                input_serialization_format="cdr",
                output_serialization_format="cdr",
            ),
        )
    except Exception as error:
        raise RuntimeError(f"cannot open MCAP bag: {error}") from error

    topic_types = {topic.name: topic.type for topic in reader.get_all_topics_and_types()}
    if TASK_EVENT_TOPIC not in topic_types:
        raise RuntimeError(f"bag does not contain {TASK_EVENT_TOPIC}")

    event_type = get_message(topic_types[TASK_EVENT_TOPIC])
    events = []
    while reader.has_next():
        topic, serialized, _ = reader.read_next()
        if topic == TASK_EVENT_TOPIC:
            events.append(deserialize_message(serialized, event_type))
    if not events:
        raise RuntimeError(f"bag contains no messages on {TASK_EVENT_TOPIC}")
    return events


def main() -> int:
    parser = argparse.ArgumentParser(description="Print TaskEvent messages from an MCAP bag")
    parser.add_argument("bag", type=Path)
    arguments = parser.parse_args()
    try:
        events = read_task_events(arguments.bag)
    except RuntimeError as error:
        print(f"TaskEvent bag audit failed: {error}", file=sys.stderr)
        return 1
    for event in events:
        print(format_event(event))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
