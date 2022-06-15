

from contextlib import contextmanager
from os.path import dirname as up 

import collections
import os
import sys


TOOLS_PATH = up(up(up(up(os.path.abspath(__file__)))))
sys.path.append(TOOLS_PATH)

from testrunner.local.command import DefaultOSContext

FUCHSIA_DIR = os.path.join(up(TOOLS_PATH), 'build', 'fuchsia')
sys.path.insert(0, FUCHSIA_DIR)

import device_target
import fvdl_target
import run_test_package

Args = collections.namedtuple('Args', ['out_dir', 'target_cpu', 'require_kvm',
                      'enable_graphics', 'hardware_gpu',
                      'with_network', 'cpu_cores', 'ram_size_mb',
                      'logs_dir', 'custom_image', 'code_coverage', 'host',
                      'node_name', 'port', 'ssh_config', 'fuchsia_out_dir',
                      'os_check', 'system_image_dir'])

FAR_LOCATION = 'out/fuchsia/gen/test/unittests/v8_unittests/v8_unittests.far'

PACKAGE_PATHS = ['out/fuchsia/gen/test/unittests/v8_unittests/v8_unittests.far']

class FuchsiaOSContext(DefaultOSContext):
  def __init__(self):
    self.command = None
  
  @contextmanager
  def context(self, options):
    self.options = options
    with fvdl_target.FvdlTarget.CreateFromArgs(self._target_args()) as target:
      self.target = target
      target.Start()

      with run_test_package.InstallTestPackage(target, [FAR_LOCATION]) as kernel_logger:
        self.kernel_logger = kernel_logger
        yield

  def _target_args(self):
    return Args(
        out_dir=self.options.outdir,
        target_cpu='x64',
        require_kvm=True,
        enable_graphics=False,
        hardware_gpu=False,
        with_network=False,
        cpu_cores=4,
        ram_size_mb=8192,
        logs_dir=None, 
        custom_image=None,
        code_coverage=None,
        host=None,
        node_name=None,
        port=None,
        ssh_config=None,
        fuchsia_out_dir=self.options.outdir,
        os_check='ignore',
        system_image_dir=None,
    )
    