Branches build coverage
======================

This is a description of V8 infrastructure for Beta and Stable branches.

Builder coverage for Beta/Stable branches is organised in 6 new consoles that reflect the same schema used for the master branch:
 - [beta.main](https://ci.chromium.org/p/v8/g/br.beta/console)
 - [beta.fyi](https://ci.chromium.org/p/v8/g/br.beta.fyi/console)
 - [beta.ports](https://ci.chromium.org/p/v8/g/br.beta.ports/console)
 - [stable.main](https://ci.chromium.org/p/v8/g/br.stable/console)
 - [stable.fyi](https://ci.chromium.org/p/v8/g/br.stable.fyi/console)
 - [stable.ports](https://ci.chromium.org/p/v8/g/br.stable.ports/console)

These consoles are the beta/stable branch counterparts of the [main](https://ci.chromium.org/p/v8/g/main/console), [fyi](https://ci.chromium.org/p/v8/g/fyi/console) and [ports](https://ci.chromium.org/p/v8/g/ports/console) consoles for the main branch.


Monitoring
======================

V8 sherrifs will be notified on any failures in builders under [beta.main](https://ci.chromium.org/p/v8/g/br.beta/console) and [stable.main](https://ci.chromium.org/p/v8/g/br.stable/console) consoles

Updating the Beta/Stable branch references
======================

In addtion the usual branch reference changes in infra/config we need to update the same references for Beta/Stable triggers and consoles.


Adding new builders
======================

To propagate a new builer addtion under main/fyi/ports consoles you need to:
 - add a builder definition in beta and stable buckets
 - create BETA and STABLE jobs and triggers
 - add the new builder references under corresponding Beta/Stable consoles
 - addtionaly for builders under main console add a reference for luci/notify
 - back merge mb_config.pyl and builders.pyl file changes to Beta/Stable branch

Developer branches
======================

On this foundation for branch builder coverage it is possible to cover more than just Beta/Stable branches. Developer branches might require full or partial (no fyi/no ports) builder coverage. To achieve that one needs to replicate the Beta/Stable configuration in infra/config branch.
