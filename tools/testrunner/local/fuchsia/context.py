import collections
import os
import time
from contextlib import contextmanager
from testrunner.objects import output


from testrunner.local.command import BaseCommand, DefaultOSContext,\
  SingleThreadedExecutionPool

class FStream():

  def __init__(self):
    self.buffer = []

  def print(self, text):
    self.buffer.append(text)


class FuchsiaCommand(BaseCommand):
  def __init__(self,
               target,
               package_paths,
               package_args,
               kernel_logger,
               shell,
               args=None,
               cmd_prefix=None,
               timeout=60,
               env=None,
               verbose=False,
               resources_func=None,
               handle_sigterm=False):
    from .run_test_package import RunTestPackageArgs
    super(FuchsiaCommand, self).__init__(
        shell,
        args=args,
        cmd_prefix=cmd_prefix,
        timeout=timeout,
        env=env,
        verbose=verbose,
        resources_func=None,
        handle_sigterm=handle_sigterm)
    self.target = target
    self.package_paths = package_paths
    self.package_args = RunTestPackageArgs.FromCommonArgs(package_args)
    self.kernel_logger = kernel_logger

  def execute(self):
    start_time = time.time()
    st = FStream()
    from .run_test_package import RunTest

    # TODO: Use correct shell from test definition.
    return_code = RunTest(
        self.target,
        None,
        self.package_paths,
        'v8_unittests',
        '1',
        self.args,
        self.package_args,
        print_stream=st)
    duration = time.time() - start_time
    return output.Output(
        return_code,
        False,  # TODO: Figure out timeouts.
        '\n'.join(st.buffer),
        '',  # No stderr available.
        -1,  # No pid available.
        duration,
    )


Args = collections.namedtuple('Args', [
    'out_dir', 'target_cpu', 'require_kvm', 'enable_graphics', 'hardware_gpu',
    'with_network', 'cpu_cores', 'ram_size_mb', 'logs_dir', 'custom_image',
    'code_coverage', 'host', 'node_name', 'port', 'ssh_config',
    'fuchsia_out_dir', 'os_check', 'system_image_dir'
])
LOCAL_HOST = '127.0.0.1'


class FuchsiaOSContext(DefaultOSContext):
  def __init__(self):
    super(FuchsiaOSContext, self).__init__(None, SingleThreadedExecutionPool())

  @contextmanager
  def context(self, options):
    from .run_test_package import InstallTestPackage, CreateFromArgs
    self.options = options
    with CreateFromArgs(self._f_args()) as target:
      self.target = target
      target.Start()
      far_location = os.path.join(self.options.outdir, 'gen', 'test',
                                  'unittests', 'v8_unittests',
                                  'v8_unittests.far')
      with InstallTestPackage(target, [far_location]) as kernel_logger:
        self.command = self.command_factory(target, far_location, kernel_logger)
        yield

  def command_factory(self, target, package_paths, kernel_logger):

    def factory(shell,
                args=None,
                cmd_prefix=None,
                timeout=60,
                env=None,
                verbose=False,
                resources_func=None,
                handle_sigterm=False):
      return FuchsiaCommand(target, package_paths,
                            self._f_args(LOCAL_HOST, target._host_ssh_port),
                            kernel_logger, shell, args, cmd_prefix, timeout,
                            env, verbose, resources_func, handle_sigterm)

    return factory

  def _f_args(self, host=None, port=None):
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
        host=host,
        node_name=None,
        port=port,
        ssh_config=None,
        fuchsia_out_dir=self.options.outdir,
        os_check='ignore',
        system_image_dir=None,
    )


# TODO(liviurau): Add documentation with diagrams to describe how context and
# its components gets initialized and eventually teared down and how does it
# interact with both tests and underlying platform specific concerns.
