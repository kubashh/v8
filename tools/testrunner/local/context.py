from contextlib import contextmanager
from testrunner.local.command import AndroidOSContext, WindowsCommand,\
  DefaultOSContext, PosixCommand
from testrunner.local.fuchsia.presence import getFuchsiaContextClass

def find_os_context_factory(target_os):
  registry = dict(
      android=AndroidOSContext,
      windows=lambda: DefaultOSContext(WindowsCommand),
      fuchsia=getFuchsiaContextClass(),
    )
  default = lambda: DefaultOSContext(PosixCommand)
  return registry.get(target_os, default)


@contextmanager
def os_context(target_os, options):
  factory = find_os_context_factory(target_os)
  context = factory()
  with context.context(options):
    yield context
