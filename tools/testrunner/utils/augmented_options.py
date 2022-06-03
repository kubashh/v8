class AugmentedOptions():
  @staticmethod
  def augment(options_object):
    dummy = AugmentedOptions()
    for func_name in dir(dummy):
      func_value = getattr(dummy, func_name) 
      if callable(func_value) and not func_name.startswith("_") and func_name != "augment":
        setattr(options_object, func_name, func_value)
    return options_object
