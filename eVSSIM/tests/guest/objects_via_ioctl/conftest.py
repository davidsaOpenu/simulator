import os
import pytest

def pytest_addoption(parser):
    parser.addoption(
        "--nvme-cli-dir",
        action="store",
        default=os.curdir,
        help="directory containing the nvme-cli executable"
    )
