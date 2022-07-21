import os
import sys

BASE_DIR = os.path.normpath(
    os.path.join(
        os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', '..'))

FUCHSIA_DIR = os.path.join(BASE_DIR, 'build', 'fuchsia')


def getFuchsiaContextClass():
  if not os.path.exists(FUCHSIA_DIR):
    return None
  from .context import FuchsiaOSContext
  return FuchsiaOSContext
