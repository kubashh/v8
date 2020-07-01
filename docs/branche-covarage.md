Branches build coverage
======================

V8 is reorganising build infrastructure for Beta and Stable branches provinding better coverage.
We are removing old Branches console containig builder for beta/stable/previous branches.

Six new consoles are introcuded following the same schema used for the main branch:
 - beta.main 
 - beta.fyi 
 - beta.ports
 - stable.main
 - stable.fyi
 - stable.ports

These consoles are the beta/stable branch counterparts of the main, fyi and port consoles for the main branch.

This new approach promises to give us additional flexibility (especially after the upcomming Starlark migration) in maintaing consitent coverage across branches. We also anticipate to extend the coverage for developer branches, should this become a necesity in the future.
