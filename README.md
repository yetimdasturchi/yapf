
# YAPF — Yet Another PHP File

YAPF takes a normal PHP project and turns it into something you can hand to a customer or deploy to a server without handing over your source code. The application is encrypted into a single sealed file, and it will only run on the machine it was built for. Copy the folder to another computer and it simply refuses to start.

This is the kind of tool you reach for when you're distributing PHP software outside your own infrastructure — selling a self-hosted product, shipping an on-premise build to a client, or licensing software by machine or by time — and you need the runtime itself to enforce that, not just a license key the app checks "out of politeness."

## How it works, in short

1.  You build YAPF's native tools once, for your own machine.
2.  You **seal** your PHP project: YAPF encrypts it into an `app.yapfc` container and generates a license tied to a specific machine id.
3.  You ship the resulting folder. On that one machine, `start` launches PHP with YAPF's loader extension active, which decrypts and runs the app. Anywhere else, it refuses.
4.  While running, the app gets exactly one writable folder (`storage/`) and a small set of PHP constants telling it whether the license is valid, how much time or how many runs are left, and so on.

Your actual PHP files stay locked inside the encrypted container for the lifetime of the deployment — they never touch disk in readable form.

## What a release folder looks like

After you seal an app, you get a self-contained folder like this:

```text
my-app/
  start              launches the app
  client             inspects machine id / license / state
  yapf_loader.so     the PHP extension that decrypts and validates at runtime
  app.yapfc          the sealed, encrypted application
  license.yapfl      immutable license metadata
  license.yapfs      mutable license state (usage counters, timestamps)
  .env               environment variables loaded before boot
  storage/           the only writable directory

```

A few things worth knowing:

-   `start` is a small native binary — not a shell script — that launches PHP with the loader extension pre-configured, then hands control to `yapf_start()`. You almost never touch PHP directly.
-   `client`, run with no arguments inside this folder, prints a readable license summary: app id, entry point, whether the machine matches, expiry, limit type, runs used, first/last seen. Run it from anywhere else with `--app /path/to/my-app` to inspect a different deployment.
-   `.env` is loaded before PHP starts, but if a variable already exists in the system environment, the system one wins. If you don't supply your own `.env` at release time, YAPF generates a minimal one pointing `APP_STORAGE` at the `storage/` folder.
-   `storage/` is intentionally the only place the app can write. Everything else in the folder is meant to stay read-only once deployed.
-   The PHP binary `start` calls is baked in at build time (`/usr/bin/php8.3` by default), but you can override it per-run with the `YAPF_PHP_BIN` environment variable without rebuilding anything.

## Why it's structured this way

The interesting design choice in YAPF is that the encryption isn't just "obfuscate the source code." Your PHP code still runs as PHP — `require`, `file_exists`, `opendir`, and friends all work with normal relative paths, because YAPF registers a `yapf://` stream wrapper that intercepts filesystem calls behind the scenes. From the application's point of view, nothing about its own logic needs to change. What changes is that the files it's reading don't exist anywhere on disk in readable form — they're decrypted into memory, file by file, only for a process holding a valid license, and the wrapper is strictly read-only.

That's also why machine-binding and licensing live at the runtime level rather than inside your application code: there's no license check to find and patch out, because the application code itself doesn't decrypt without one. The check runs once per process, before a single line of your PHP executes — `yapf_start()` validates the license first and refuses to even load your entry file if it fails.

## Setting it up

### What you need

Building YAPF's native pieces requires a normal C toolchain plus PHP's development tools:

-   A C compiler and `make`
-   PHP CLI, `phpize`, and `php-config`
-   The `openssl` CLI, used to generate random values at release time

If you have multiple PHP versions installed, point at the right ones explicitly, e.g. for PHP 8.3:

```sh
PHP_BIN=/usr/bin/php8.3
PHPIZE=/usr/bin/phpize8.3
PHP_CONFIG=/usr/bin/php-config8.3

```

### Building from source

Before your first build, generate a salt and secret. These two values are baked directly into the compiled binaries and are effectively the "family key" for everything you build afterward — every package sealed with this pair will only work with the `start` binary and `yapf_loader.so` built alongside it. Lose them and you can't validate existing licenses; mix builds from different pairs and nothing will run.

```sh
export YAPF_MACHINE_SALT="$(openssl rand -hex 32)"
export YAPF_CRYPTO_SECRET="$(openssl rand -hex 32)"

scripts/build.sh --source . all \
  PHP_BIN=/usr/bin/php8.3 \
  PHPIZE=/usr/bin/phpize8.3 \
  PHP_CONFIG=/usr/bin/php-config8.3

```

This builds two things: the **native tools** (`yapf-pack`, `yapf-seal`, `yapf-client`, `start`) and the **loader**, which is compiled as an actual PHP extension via `phpize`/`./configure`/`make` against your PHP install. If you only need part of the toolchain:

```sh
scripts/build.sh --source . native
scripts/build.sh --source . loader \
  PHPIZE=/usr/bin/phpize8.3 \
  PHP_CONFIG=/usr/bin/php-config8.3

```

Everything lands under `build/` — `build/bin/` for the CLI tools and `start`, `build/loader/yapf_loader.so` for the extension.

### Installing system-wide

Once built, install the tools so `yapf-release`, `yapf-pack`, etc. are available on your `PATH`:

```sh
sudo scripts/install.sh --strip

```

This places everything in a standard layout:

```text
/etc/yapf.conf
/usr/bin/yapf-build
/usr/bin/yapf-release
/usr/bin/yapf-pack
/usr/bin/yapf-seal
/usr/bin/yapf-client
/usr/lib/yapf/start
/usr/lib/yapf/yapf_loader.so
/usr/lib/yapf/client

```

`yapf-client` is actually a symlink into `/usr/lib/yapf/client`, so the same binary serves both the system tool and the copy that ships inside every release folder.

If you're packaging YAPF for distribution rather than installing it directly, use a staged prefix instead:

```sh
scripts/install.sh \
  --prefix /tmp/yapf-install/usr \
  --sysconfdir /tmp/yapf-install/etc

```

## Releasing an app

This is the everyday workflow once everything is built and installed.

**Step 1 — find out the target machine's id.** Run this on the machine the app will eventually run on:

```sh
build/bin/yapf-client --raw

```

**Step 2 — seal the app for that machine.** This is the core command: it reads your PHP project, encrypts it, and writes out a complete runtime folder. Behind the scenes it actually runs three tools — `yapf-pack` to build the container, then `yapf-seal` twice to write the matching license and state files — but `release.sh` wraps all of that into one call.

```sh
scripts/release.sh \
  --source /path/to/php-project \
  --output /path/to/dist/my-app \
  --app-id my-app \
  --entry public/index.php \
  --machine-code "YAPF-..." \
  --strip

```

**Step 3 — ship and run it.** Copy `/path/to/dist/my-app` to the target machine and run:

```sh
/path/to/dist/my-app/start

```

**Step 4 — check on it later, if you need to.** Whether for support or just curiosity, inspect the license at any time:

```sh
/path/to/dist/my-app/client

```

### Avoiding repetitive flags with a release config

If you're releasing the same project repeatedly, typing out `--source`, `--output`, `--entry`, etc. every time gets old fast. Save them once instead:

```sh
cp config/release.conf.example /path/to/project/release.conf
$EDITOR /path/to/project/release.conf
scripts/release.sh --config /path/to/project/release.conf

```

Any flag you pass on the command line still overrides the config file, so this is purely a convenience — nothing is locked in.

Here's what each key actually controls:

**`YAPF_RELEASE_SOURCE_DIR`** — the PHP project folder that gets packed. Example: `/path/to/php-project`.

**`YAPF_RELEASE_OUTPUT_DIR`** — where the finished runtime app is written. This is the folder that ends up containing `start`, `app.yapfc`, `license.yapfl`, `.env`, and `storage/`. Example: `/path/to/dist/my-app`.

**`YAPF_RELEASE_APP_ID`** — the app's identifier. The license, the state file, and the sealed container are all bound to this id and cross-checked against each other at every startup, so it has to stay consistent across releases of the same app. Example: `my-app`.

**`YAPF_RELEASE_ENTRY`** — the PHP file that actually runs when the app starts, relative to the source root. Example: `public/index.php`.

**`YAPF_RELEASE_MACHINE_CODE`** — the id of the machine this build is licensed to run on. Get it by running `client --raw` (or `yapf-client --raw`) on the target machine. Example: `YAPF-...`.

**`YAPF_RELEASE_OVERLAY_DIR`** _(optional)_ — a folder of files copied on top of a temporary copy of the source, right before packing — your original project directory is never touched. Useful for swapping in a production config or removing something for one specific release without branching the source.

**`YAPF_RELEASE_ENV_FILE`** _(optional)_ — a `.env` file to copy into the output as `.env`. Leave it empty and YAPF writes a minimal one for you automatically (just `APP_ENV=production` and `APP_STORAGE` pointing at the release's `storage/` folder).

**`YAPF_RELEASE_EXPIRES_AT`** — when the license expires. Accepts `null` (never), a `YYYY-MM-DD` date, or an epoch timestamp.

**`YAPF_RELEASE_LIMIT_TYPE`** and **`YAPF_RELEASE_LIMIT_VALUE`** — the usage limit, used as a pair. `none` + `0` means no limit; `days` + `30` means the license is good through 30 calendar days counted from the first successful start; `runs` + `100` means it's good for 100 successful starts and then stops working. (`none` with a nonzero value, or `days`/`runs` with `0`, are both rejected at seal time.)

**`YAPF_RELEASE_STRIP`** — when `true`, strips debug symbols from `start`, `client`, and `yapf_loader.so` in the output, shrinking them and making them harder to pick apart.

**`YAPF_RELEASE_EXCLUDE`** — folders or files left out of the package entirely. Matching isn't just an exact path — `tests` will also exclude `app/tests`, `src/tests/Unit`, anything where `tests` appears as a whole path segment. Example: `(tests docs)`.

**`YAPF_RELEASE_ALLOW_EXT`** — restricts packing to only files with these extensions. If you don't set this at all, every file that isn't otherwise excluded gets packed, regardless of extension. Example: `php,json,lock,xml,yml,yaml,ini,stub`.

**`YAPF_RELEASE_ALLOW_FILE`** — specific files included by exact path even when `ALLOW_EXT` is active and they wouldn't otherwise match — useful for extensionless or oddly-named files like `composer.lock` or `artisan`. This has no effect unless `ALLOW_EXT` is also set. Example: `(composer.lock artisan)`.

Separately, the _tool_ paths themselves (where `yapf-pack`, `yapf-seal`, etc. actually live) are configured in `/etc/yapf.conf`:

```sh
YAPF_LIB_DIR=/usr/lib/yapf
YAPF_BIN_DIR=/usr/bin
YAPF_PACK_BIN=/usr/bin/yapf-pack
YAPF_SEAL_BIN=/usr/bin/yapf-seal
YAPF_START_BIN=/usr/lib/yapf/start
YAPF_LOADER_SO=/usr/lib/yapf/yapf_loader.so
YAPF_CLIENT_BIN=/usr/lib/yapf/client

```

If you're running `release.sh` from inside a built-but-not-installed checkout, it'll automatically prefer the binaries in `build/` over anything in `/etc/yapf.conf` — handy for testing a release before installing system-wide.

### Controlling exactly what gets packed

Pack filters can also be passed directly on the command line, which `release.sh` forwards straight through to `yapf-pack`:

```sh
scripts/release.sh \
  --source /path/to/php-project \
  --output /path/to/dist/my-app \
  --app-id my-app \
  --entry public/index.php \
  --machine-code "YAPF-..." \
  --exclude tests \
  --allow-ext php,json,lock,xml,yml,yaml,ini,stub \
  --allow-file composer.lock \
  --allow-file artisan

```

A few files and folders are excluded automatically, no matter what you pass:

```text
.env
.git
.svn
node_modules
storage
var/cache
bootstrap/cache

```

(Like custom excludes, these match anywhere the name appears as a path segment, not just at the root.)

## How licensing actually works

A license can be unrestricted, time-limited, or usage-limited:

| `LIMIT_TYPE` | `LIMIT_VALUE` | Meaning |
|---|---|---|
| `none` | `0` | No usage limit at all |
| `days` | `30` | Valid through 30 days, counted from the first successful start |
| `runs` | `100` | Valid for 100 successful starts, ever |

`EXPIRES_AT` works alongside this independently and accepts `null`, a `YYYY-MM-DD` date, or a raw epoch timestamp — a license can have both an expiry date and a run limit at once.

Every time the app starts, the loader independently verifies, in order: that the app, license, and state files all share the same app id; that the machine code embedded in both the license and the state file matches the current machine; that the license's state id matches the state file's; that the state file's seed actually hashes to the value the license expects (this is what stops someone from substituting a different state file to reset a run counter); that the license hasn't expired; that the system clock hasn't visibly jumped backward (a five-minute tolerance is allowed for normal clock drift); and finally that the day or run limit, if any, hasn't been exceeded. Only after every one of those passes does PHP actually get to execute, and the state file's run/day counters are updated and re-saved as part of the same check — there's no separate "record a run" step.

Once validation passes, the runtime exposes a handful of read-only constants so your app can react to its own license state if it wants to — showing "3 days left," for instance:

```php
YAPF_LICENSE_VALID
YAPF_APP_ID
YAPF_MACHINE_CODE
YAPF_LICENSE_EXPIRES_AT
YAPF_LICENSE_LIMIT_TYPE
YAPF_LICENSE_LIMIT_VALUE
YAPF_LICENSE_COUNTER
YAPF_LICENSE_LAST_SEEN_AT
YAPF_STORAGE_PATH

```

There's also a small helper you can call any time, which returns an associative array rather than just printing something — handy for building your own "license status" admin page:

```php
$info = yapf_info();
// ['status' => 'loaded', 'license_ok' => true, 'app_id' => 'my-app',
//  'machine_code' => '...', 'expires_at' => 'null', 'limit_type' => 'none',
//  'limit_value' => '0', 'license_counter' => '0', 'last_seen_at' => '...',
//  'storage_path' => '/path/to/my-app/storage']

```

If the license check fails, `yapf_info()` still returns gracefully with `license_ok => false` and a `license_error` message — `yapf_start()` itself, on the other hand, prints the error and exits the process.

## Project layout (for contributors)

If you're working on YAPF itself rather than just using it, here's where things live:

```text
config/
examples/
include/
scripts/
src/
Makefile

```

-   `src/cli/` — the native CLI entrypoints: `pack.c`, `seal.c`, `client.c`
-   `src/runtime/` — the `start` launcher (`start.c`) and the `.env` loader it uses (`env.c`)
-   `src/extension/` — the PHP extension itself: `license.c` does the validation walk described above, `stream.c` implements the `yapf://` wrapper (open/stat/readdir, all read-only)
-   `src/core/` — the sealed container format and the line-based key/value payload parser shared by everything above
-   `src/crypto/` — internal sealing, hashing, and machine-id derivation
-   `include/` — shared headers, mirroring the `src/` layout
-   `scripts/` — `build.sh`, `release.sh`, `install.sh`, described above

`build/` and `out/` are generated locally and git-ignored. Note that `scripts/build.sh loader` doesn't compile the extension directly out of `src/extension/` — it stages a fresh copy of the relevant source files plus the generated machine-key header into `build/php-ext/` and runs the normal `phpize` extension build there, so the loader ends up self-contained.

## Two worked examples

**A minimal PHP app**, sealed and run end to end:

```sh
machine_code="$(build/bin/yapf-client --raw)"

scripts/release.sh \
  --source examples/minimal-php \
  --output out/apps/minimal \
  --app-id minimal \
  --entry public/index.php \
  --machine-code "$machine_code" \
  --strip

out/apps/minimal/start hello

```

**A CLI app that reads from stdin**, to confirm interactive input passes through correctly:

```sh
machine_code="$(build/bin/yapf-client --raw)"

scripts/release.sh \
  --source examples/input-cli \
  --output out/apps/input-cli \
  --app-id input-cli \
  --entry input.php \
  --machine-code "$machine_code" \
  --allow-ext php \
  --strip

out/apps/input-cli/start

```

## What YAPF does and doesn't protect against

It's worth being precise about the threat model here. YAPF secures the _deployment artifact_: your source isn't sitting on disk in plaintext, the package won't run on a machine it wasn't issued for, and the license state is sealed and self-validating rather than just a flag in a config file someone could edit. That's a meaningfully higher bar than "just zip the PHP files."

What it doesn't do is replace ordinary server security. If someone has root on the machine the app is licensed to run on, they have the same access any running process has — the decrypted PHP source exists in that process's memory while it runs, and YAPF isn't a substitute for filesystem permissions, process isolation, backups, or monitoring on that host. Treat it as one layer in a deployment, not the whole defense.

Finally: `YAPF_MACHINE_SALT` and `YAPF_CRYPTO_SECRET` are the keys to everything you've sealed, baked directly into your compiled binaries. Keep them private, and if you ever need to rotate them, rebuild the native tools and the loader together — a mismatched pair simply won't be able to talk to each other, and existing licenses sealed under the old pair won't validate against new binaries.