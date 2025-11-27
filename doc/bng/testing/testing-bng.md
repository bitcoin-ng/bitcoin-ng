# Rebranding & Test FAQ

This file contains concise, high-level answers to common questions about running the repository tests and validating rebranding-related changes.

## FAQ

- **Q: How do I run util tests?**

	- **A (quick)**: From the repo root, run:

		```bash
		make check
		```

	- **A (explicit runner)**: You can run the util test runner directly for more control and verbosity:

		```bash
		test/util/test_runner.py -v
		```

- **Q: How do I prepare for functional tests?**

	- **A (dependencies)**: Ensure Python and required libraries are installed. On Debian/Ubuntu:

		```bash
		sudo apt update
		sudo apt install -y python3 python3-pip python3-zmq
		# If your distro lacks python3-zmq package:
		pip3 install --user pyzmq
		```

	- **A (stop running nodes to avoid port conflicts)**: Make sure no stray `bitcoind` processes are running before starting functional tests:

		```bash
		killall bitcoind || true
		# or, more forceful:
		pkill -9 bitcoind || true
		```

	- **A (Windows note)**: On Windows set `PYTHONUTF8=1` in the environment before running tests.

- **Q: How do I run functional tests?**

	- **A (single test)**:

		```bash
		test/functional/test_runner.py test/functional/feature_rbf.py
		```

	- **A (regression suite)**: Run the default regression suite (runs tests in parallel, default jobs = 4):

		```bash
		test/functional/test_runner.py
		```

	- **A (all tests / extended)**:

		```bash
		test/functional/test_runner.py --extended
		```

	- **A (jobs, cache, tmpdir)**: To tune parallelism and use a RAM disk or cache:

		```bash
		test/functional/test_runner.py --jobs=8 --cachedir=/mnt/tmp/cache --tmpdir=/mnt/tmp
		```

- **Q: What should I do if tests fail due to environment or leftover processes?**

	- **A (quick cleanup)**:

		```bash
		pkill -9 bitcoind || true
		rm -rf test/cache
		```

	- **A (collect logs)**: Use the combine logs helper to inspect test logs:

		```bash
		test/functional/combine_logs.py -c <test data directory> | less -r
		```

	- **A (debugging tips)**: Use `--nocleanup` to keep test data, add `import pdb; pdb.set_trace()` in tests, or run with `--timeout-factor 0` to disable RPC timeouts when debugging.

## Short guidance for rebranding checks

- Use the rebrand mapping + detection scripts (see `doc/bng/testing/rebrand-mapping.txt` and `doc/bng/testing/check-rebrand.sh` if present) to scan the repo for old branding strings.
- Run the detection script in `report` mode first, then `strict` mode in CI to block regressions.

If you'd like, I can add the mapping and the `check-rebrand.sh` script now and run an initial scan on the current branch — tell me to proceed and I'll create the files and run them.

