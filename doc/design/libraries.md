# Libraries

| Name                     | Description |
|--------------------------|-------------|
| *libelevenseventyfive_cli*         | RPC client functionality used by *elevenseventyfive-cli* executable |
| *libelevenseventyfive_common*      | Home for common functionality shared by different executables and libraries. Similar to *libelevenseventyfive_util*, but higher-level (see [Dependencies](#dependencies)). |
| *libelevenseventyfive_consensus*   | Stable, backwards-compatible consensus functionality used by *libelevenseventyfive_node* and *libelevenseventyfive_wallet* and also exposed as a [shared library](../shared-libraries.md). |
| *libelevenseventyfiveconsensus*    | Shared library build of static *libelevenseventyfive_consensus* library |
| *libelevenseventyfive_kernel*      | Consensus engine and support library used for validation by *libelevenseventyfive_node* and also exposed as a [shared library](../shared-libraries.md). |
| *libelevenseventyfiveqt*           | GUI functionality used by *elevenseventyfive-qt* and *elevenseventyfive-gui* executables |
| *libelevenseventyfive_ipc*         | IPC functionality used by *elevenseventyfive-node*, *elevenseventyfive-wallet*, *elevenseventyfive-gui* executables to communicate when [`--enable-multiprocess`](multiprocess.md) is used. |
| *libelevenseventyfive_node*        | P2P and RPC server functionality used by *elevenseventyfived* and *elevenseventyfive-qt* executables. |
| *libelevenseventyfive_util*        | Home for common functionality shared by different executables and libraries. Similar to *libelevenseventyfive_common*, but lower-level (see [Dependencies](#dependencies)). |
| *libelevenseventyfive_wallet*      | Wallet functionality used by *elevenseventyfived* and *elevenseventyfive-wallet* executables. |
| *libelevenseventyfive_wallet_tool* | Lower-level wallet functionality used by *elevenseventyfive-wallet* executable. |
| *libelevenseventyfive_zmq*         | [ZeroMQ](../zmq.md) functionality used by *elevenseventyfived* and *elevenseventyfive-qt* executables. |

## Conventions

- Most libraries are internal libraries and have APIs which are completely unstable! There are few or no restrictions on backwards compatibility or rules about external dependencies. Exceptions are *libelevenseventyfive_consensus* and *libelevenseventyfive_kernel* which have external interfaces documented at [../shared-libraries.md](../shared-libraries.md).

- Generally each library should have a corresponding source directory and namespace. Source code organization is a work in progress, so it is true that some namespaces are applied inconsistently, and if you look at [`libelevenseventyfive_*_SOURCES`](../../src/Makefile.am) lists you can see that many libraries pull in files from outside their source directory. But when working with libraries, it is good to follow a consistent pattern like:

  - *libelevenseventyfive_node* code lives in `src/node/` in the `node::` namespace
  - *libelevenseventyfive_wallet* code lives in `src/wallet/` in the `wallet::` namespace
  - *libelevenseventyfive_ipc* code lives in `src/ipc/` in the `ipc::` namespace
  - *libelevenseventyfive_util* code lives in `src/util/` in the `util::` namespace
  - *libelevenseventyfive_consensus* code lives in `src/consensus/` in the `Consensus::` namespace

## Dependencies

- Libraries should minimize what other libraries they depend on, and only reference symbols following the arrows shown in the dependency graph below:

<table><tr><td>

```mermaid

%%{ init : { "flowchart" : { "curve" : "basis" }}}%%

graph TD;

elevenseventyfive-cli[elevenseventyfive-cli]-->libelevenseventyfive_cli;

elevenseventyfived[elevenseventyfived]-->libelevenseventyfive_node;
elevenseventyfived[elevenseventyfived]-->libelevenseventyfive_wallet;

elevenseventyfive-qt[elevenseventyfive-qt]-->libelevenseventyfive_node;
elevenseventyfive-qt[elevenseventyfive-qt]-->libelevenseventyfiveqt;
elevenseventyfive-qt[elevenseventyfive-qt]-->libelevenseventyfive_wallet;

elevenseventyfive-wallet[elevenseventyfive-wallet]-->libelevenseventyfive_wallet;
elevenseventyfive-wallet[elevenseventyfive-wallet]-->libelevenseventyfive_wallet_tool;

libelevenseventyfive_cli-->libelevenseventyfive_util;
libelevenseventyfive_cli-->libelevenseventyfive_common;

libelevenseventyfive_common-->libelevenseventyfive_consensus;
libelevenseventyfive_common-->libelevenseventyfive_util;

libelevenseventyfive_kernel-->libelevenseventyfive_consensus;
libelevenseventyfive_kernel-->libelevenseventyfive_util;

libelevenseventyfive_node-->libelevenseventyfive_consensus;
libelevenseventyfive_node-->libelevenseventyfive_kernel;
libelevenseventyfive_node-->libelevenseventyfive_common;
libelevenseventyfive_node-->libelevenseventyfive_util;

libelevenseventyfiveqt-->libelevenseventyfive_common;
libelevenseventyfiveqt-->libelevenseventyfive_util;

libelevenseventyfive_wallet-->libelevenseventyfive_common;
libelevenseventyfive_wallet-->libelevenseventyfive_util;

libelevenseventyfive_wallet_tool-->libelevenseventyfive_wallet;
libelevenseventyfive_wallet_tool-->libelevenseventyfive_util;

classDef bold stroke-width:2px, font-weight:bold, font-size: smaller;
class elevenseventyfive-qt,elevenseventyfived,elevenseventyfive-cli,elevenseventyfive-wallet bold
```
</td></tr><tr><td>

**Dependency graph**. Arrows show linker symbol dependencies. *Consensus* lib depends on nothing. *Util* lib is depended on by everything. *Kernel* lib depends only on consensus and util.

</td></tr></table>

- The graph shows what _linker symbols_ (functions and variables) from each library other libraries can call and reference directly, but it is not a call graph. For example, there is no arrow connecting *libelevenseventyfive_wallet* and *libelevenseventyfive_node* libraries, because these libraries are intended to be modular and not depend on each other's internal implementation details. But wallet code is still able to call node code indirectly through the `interfaces::Chain` abstract class in [`interfaces/chain.h`](../../src/interfaces/chain.h) and node code calls wallet code through the `interfaces::ChainClient` and `interfaces::Chain::Notifications` abstract classes in the same file. In general, defining abstract classes in [`src/interfaces/`](../../src/interfaces/) can be a convenient way of avoiding unwanted direct dependencies or circular dependencies between libraries.

- *libelevenseventyfive_consensus* should be a standalone dependency that any library can depend on, and it should not depend on any other libraries itself.

- *libelevenseventyfive_util* should also be a standalone dependency that any library can depend on, and it should not depend on other internal libraries.

- *libelevenseventyfive_common* should serve a similar function as *libelevenseventyfive_util* and be a place for miscellaneous code used by various daemon, GUI, and CLI applications and libraries to live. It should not depend on anything other than *libelevenseventyfive_util* and *libelevenseventyfive_consensus*. The boundary between _util_ and _common_ is a little fuzzy but historically _util_ has been used for more generic, lower-level things like parsing hex, and _common_ has been used for elevenseventyfive-specific, higher-level things like parsing base58. The difference between util and common is mostly important because *libelevenseventyfive_kernel* is not supposed to depend on *libelevenseventyfive_common*, only *libelevenseventyfive_util*. In general, if it is ever unclear whether it is better to add code to *util* or *common*, it is probably better to add it to *common* unless it is very generically useful or useful particularly to include in the kernel.


- *libelevenseventyfive_kernel* should only depend on *libelevenseventyfive_util* and *libelevenseventyfive_consensus*.

- The only thing that should depend on *libelevenseventyfive_kernel* internally should be *libelevenseventyfive_node*. GUI and wallet libraries *libelevenseventyfiveqt* and *libelevenseventyfive_wallet* in particular should not depend on *libelevenseventyfive_kernel* and the unneeded functionality it would pull in, like block validation. To the extent that GUI and wallet code need scripting and signing functionality, they should be get able it from *libelevenseventyfive_consensus*, *libelevenseventyfive_common*, and *libelevenseventyfive_util*, instead of *libelevenseventyfive_kernel*.

- GUI, node, and wallet code internal implementations should all be independent of each other, and the *libelevenseventyfiveqt*, *libelevenseventyfive_node*, *libelevenseventyfive_wallet* libraries should never reference each other's symbols. They should only call each other through [`src/interfaces/`](`../../src/interfaces/`) abstract interfaces.

## Work in progress

- Validation code is moving from *libelevenseventyfive_node* to *libelevenseventyfive_kernel* as part of [The libelevenseventyfivekernel Project #24303](https://github.com/elevenseventyfive/elevenseventyfive/issues/24303)
- Source code organization is discussed in general in [Library source code organization #15732](https://github.com/elevenseventyfive/elevenseventyfive/issues/15732)
