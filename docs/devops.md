# Numeraire++ — DevOps runbook

Cheat sheet for the two Linux hosts. Crontab lives **on the machine** (not in git). Code deploys to prod via GitHub Actions (`deploy.sh` → `git pull`). Do **not** copy `db.sqlite3` between hosts — books differ.

Times below are **UTC**. Poland is UTC+2 (CEST). Calendar: **Tue–Sat** (`2-6`) so the job covers the previous US session after Polygon T+1 lag.

**As of 2026-09-10.**

---

## Hosts

| | **Dev (Hetzner)** | **Prod (AWS)** |
|--|-------------------|----------------|
| Tree | `/opt/numeraire/dev` | `/opt/numeraire/prod` |
| SQLite | `/opt/numeraire/dev/db.sqlite3` | `/opt/numeraire/prod/db.sqlite3` |
| Crontab | **root** (`crontab -l`) | **ubuntu** (`crontab -l`, not `sudo`) |
| Live logs | `/var/log/numeraire-{prep,mtm,exposure}.log` | `/opt/numeraire/prod/logs/numeraire-{prep,mtm,exposure}.log` |
| Archive (session `as_of`) | `/var/log/numeraire-archive/` | `/opt/numeraire/prod/logs/archive/` |
| `NUMERAIRE_LOG_DIR` | unset (default `/var/log`) | `/opt/numeraire/prod/logs` |

---

## Three daily jobs

Booking (`--price-booking`) is **manual**. Cron is only market data + FO mark + CCR.

| # | Script | What |
|---|--------|------|
| 1 | `scripts/daily_market_prep.sh` | Polygon / Massive ingest (equities, NDX, vol, FRED T-2, futures strip + session EOD). |
| 2 | `scripts/daily_book_mtm.sh` | FO MTM for `LIVE` trades → `trade_leg_mtm_eod`. **No** CCR. |
| 3 | `scripts/daily_book_exposure.sh` | After MTM: EE / PFE 95% / 97.5% → `trade_leg_exposure_eod`. Refused if any LIVE leg lacks official MTM on that `as_of`. |

`daily_book_mtm.sh` does **not** call exposure. `NUMERAIRE_SKIP_EXPOSURE=1` is only a kill-switch on the **exposure** script.

Default `as_of`: previous Mon–Fri, **UTC**, `NUMERAIRE_AS_OF_LAG_DAYS=1`. US holidays are **not** skipped. FRED curve uses lag **2**.

---

## Schedule (prod first, then Hetzner)

Same Polygon API key: **do not start both preps in the same minute**. Prod is the book of record → **prod prep starts 30 min earlier**. MTM / exposure are local SQLite; the same 30 min stagger is kept for a single rhythm.

| Job | Prod UTC | Prod PL | Hetzner UTC | Hetzner PL |
|-----|----------|---------|-------------|------------|
| prep | `0 4` | 06:00 | `30 4` | 06:30 |
| MTM | `0 6` | 08:00 | `30 6` | 08:30 |
| exposure | `30 6` | 08:30 | `0 7` | 09:00 |

Typical runtime (Hetzner, 11 LIVE trades): prep ~7 min, MTM + exposure a few seconds. Chain is meant to be **done by ~09:00 PL** (Hetzner last job; prod earlier).

### Prod crontab (`ubuntu`)

```cron
SHELL=/bin/bash
PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
NUMERAIRE_LOG_DIR=/opt/numeraire/prod/logs

0 4 * * 2-6 cd /opt/numeraire/prod && ./scripts/daily_market_prep.sh >> /opt/numeraire/prod/logs/numeraire-prep.log 2>&1
0 6 * * 2-6 cd /opt/numeraire/prod && ./scripts/daily_book_mtm.sh >> /opt/numeraire/prod/logs/numeraire-mtm.log 2>&1
30 6 * * 2-6 cd /opt/numeraire/prod && ./scripts/daily_book_exposure.sh >> /opt/numeraire/prod/logs/numeraire-exposure.log 2>&1
```

Replace the whole crontab from the shell (nano is painful). `Ctrl+X` then `N` if you opened `crontab -e` by mistake:

```bash
crontab - <<'EOF'
# paste the block above
EOF
crontab -l
```

### Hetzner crontab (`root`)

```cron
SHELL=/bin/bash
PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

30 4 * * 2-6 cd /opt/numeraire/dev && ./scripts/daily_market_prep.sh >> /var/log/numeraire-prep.log 2>&1
30 6 * * 2-6 cd /opt/numeraire/dev && ./scripts/daily_book_mtm.sh >> /var/log/numeraire-mtm.log 2>&1
0 7 * * 2-6 cd /opt/numeraire/dev && ./scripts/daily_book_exposure.sh >> /var/log/numeraire-exposure.log 2>&1
```

---

## Logs

Crontab always appends to **three undated files** = last run (quick look). After `as_of` is known, scripts `tee` a copy into **archive** named with the **session date**, not the rotation calendar.

| | Live (latest) | Archive (that `as_of`) |
|--|---------------|-------------------------|
| Hetzner | `/var/log/numeraire-mtm.log` | `/var/log/numeraire-archive/numeraire-mtm-YYYY-MM-DD.log` |
| Prod | `…/prod/logs/numeraire-mtm.log` | `…/prod/logs/archive/numeraire-mtm-YYYY-MM-DD.log` |

Same pattern for `prep` and `exposure`.

```bash
# latest run
lnav /var/log/numeraire-mtm.log
lnav /opt/numeraire/prod/logs/numeraire-mtm.log

# one session
lnav /var/log/numeraire-archive/numeraire-mtm-2026-09-09.log
lnav /opt/numeraire/prod/logs/archive/numeraire-mtm-2026-09-09.log
```

lnav: `G` end of file, `e` next `[error]`, `/CLX6` search. Bash lines look like `[2026-09-10T08:30:01Z] …`; C++ like `[2026-09-10 08:30:01] [error] …`.

Look for `as_of log → …` near the start of a run (tee hooked). `failed=0` at end of prep.

**logrotate** empties the **undated** trio nightly (`rotate 0`, no `dateext`). Archive slices are left alone.

Hetzner: copy repo template (root, `/var/log`):

```bash
sudo cp /opt/numeraire/dev/scripts/logrotate.d/numeraire /etc/logrotate.d/numeraire
sudo logrotate -d /etc/logrotate.d/numeraire
```

Prod: **do not** copy that template (wrong paths). Own file, `su ubuntu ubuntu`:

```text
/opt/numeraire/prod/logs/numeraire-prep.log
/opt/numeraire/prod/logs/numeraire-mtm.log
/opt/numeraire/prod/logs/numeraire-exposure.log {
    su ubuntu ubuntu
    daily
    missingok
    notifempty
    nocompress
    rotate 0
    create 0644 ubuntu ubuntu
}
```

`numeraire-exposure.log` appears on the first exposure cron (`>>` creates it).

`scripts/logrotate.d/` is ignored by git (`*.d`). Treat `/etc/logrotate.d/numeraire` as host config.

---

## Manual futures settles (Massive gaps)

Both hosts still **ingest Massive** for listed front tenors. When the strip is incomplete (holiday `window_start`, far contracts), MTM fail-fast looks like:

```text
missing futures EOD settle for ticker=CLX6 as_of=…
```

Excel paste is **Hetzner only**. Workbook is **not** in git (`*.xlsx`). Do not `--apply` the parser on prod.

Workbook `configs/cme_manual_settles.xlsx` (local file):

- A1 = `As Of Date`, B1 = `YYYY-MM-DD`
- sheets named `CL`, `NG`, …; CME headers including MONTH and SETTLE

```bash
cd /opt/numeraire/dev
python3 scripts/parse_cme_manual_settles.py                 # dry-run HAVE / WOULD_INSERT
python3 scripts/parse_cme_manual_settles.py --sheets CL,NG  # comma, not space
python3 scripts/parse_cme_manual_settles.py --apply         # INSERT missing only, source=cme_manual
```

Never UPDATEs an existing `(ticker, as_of, 1session)` row.

### Promote those rows to prod (not the book)

JSON dump of `futures_daily_eod` where `source=cme_manual`. No trades.

```bash
# Dev — one session (omit --as-of only for a full history dump)
python3 scripts/sync_cme_manual_settles.py export --as-of 2026-09-08 --out /tmp/cme_manual.json

scp /tmp/cme_manual.json ubuntu@PROD:/tmp/cme_manual.json

# Prod — dry-run then insert gaps (SKIP_HAVE = Massive already has the bar)
cd /opt/numeraire/prod
python3 scripts/sync_cme_manual_settles.py import --from /tmp/cme_manual.json
python3 scripts/sync_cme_manual_settles.py import --from /tmp/cme_manual.json --apply
```

Then, if MTM/exposure already failed that morning:

```bash
cd /opt/numeraire/prod   # or /opt/numeraire/dev
NUMERAIRE_AS_OF=2026-09-08 ./scripts/daily_book_mtm.sh
NUMERAIRE_AS_OF=2026-09-08 ./scripts/daily_book_exposure.sh
```

Not a cron job. Prod does not SSH to Hetzner.

---

## Manual backfill (missed day / holiday)

From the **repository root** of that host. Set `NUMERAIRE_AS_OF` per **session** date (the `as_of` you want in SQLite, usually yesterday’s US session). LIVE legs need `execution_price > 0`.

```bash
cd /opt/numeraire/dev    # or prod

NUMERAIRE_DRY_RUN=1 ./scripts/daily_market_prep.sh
NUMERAIRE_DRY_RUN=1 ./scripts/daily_book_mtm.sh
NUMERAIRE_DRY_RUN=1 ./scripts/daily_book_exposure.sh

# market data only
for d in 2026-09-08 2026-09-09; do
  NUMERAIRE_AS_OF=$d ./scripts/daily_market_prep.sh
done

# marks only (data already in DB)
NUMERAIRE_AS_OF=2026-09-08 ./scripts/daily_book_mtm.sh

# CCR only (official FO MTM must already exist)
NUMERAIRE_AS_OF=2026-09-08 ./scripts/daily_book_exposure.sh

# full day
for d in 2026-09-08 2026-09-09; do
  NUMERAIRE_AS_OF=$d ./scripts/daily_market_prep.sh
  NUMERAIRE_AS_OF=$d ./scripts/daily_book_mtm.sh
  NUMERAIRE_AS_OF=$d ./scripts/daily_book_exposure.sh
done
```

Need a Release `dev_main` (`./scripts/build.sh`). Prod rebuilds on each Actions deploy.

Skip pieces: `NUMERAIRE_PREP_SKIP_FUTURES=1`, `NUMERAIRE_PREP_SKIP_FRED_CURVE=1`, `NUMERAIRE_SKIP_EXPOSURE=1` (exposure script only).

Check marks:

```bash
sqlite3 db.sqlite3 "
  SELECT as_of, batch_run_id, COUNT(*) AS legs
  FROM trade_leg_mtm_eod_archive
  WHERE as_of >= '2026-09-08'
  GROUP BY as_of, batch_run_id
  ORDER BY as_of;
"
```

---

## Deploy vs crontab

| In git + Actions | Host-only |
|------------------|-----------|
| Scripts, `lib_cron_log.sh`, `sync_cme_manual_settles.py` | `crontab` |
| | `/etc/logrotate.d/numeraire` |
| | `.env`, `db.sqlite3`, xlsx, JSON dumps |

After a push to `main`, wait for deploy, then `git log -1` on prod. Crontab does not update itself.

Deprecated: `scripts/daily_dev_eod.sh` (wrapper prep → MTM → exposure). Prefer the three jobs.

---

## Related code

- Jobs: [`scripts/daily_market_prep.sh`](../scripts/daily_market_prep.sh), [`daily_book_mtm.sh`](../scripts/daily_book_mtm.sh), [`daily_book_exposure.sh`](../scripts/daily_book_exposure.sh)
- Tee / archive: [`scripts/lib_cron_log.sh`](../scripts/lib_cron_log.sh)
- CME paste: [`scripts/parse_cme_manual_settles.py`](../scripts/parse_cme_manual_settles.py)
- Dev → prod rows: [`scripts/sync_cme_manual_settles.py`](../scripts/sync_cme_manual_settles.py)
- Schema / futures tables: [`sql/README.md`](../sql/README.md)
- Sprint notes: [`development.md`](development.md)
