(function () {
  'use strict';

  const root = document.getElementById('hypo-root');
  const bootEl = document.getElementById('hypo-lab-boot');
  if (!root || !bootEl) return;

  const boot = JSON.parse(bootEl.textContent);
  const priceUrl = root.dataset.priceUrl;
  const sampleUrl = root.dataset.sampleUrl;
  const storagePrefix = boot.storage_key_prefix || 'numeraire.hypo.';
  const maxLegs = boot.max_instruments || 3;
  const maxSteps = boot.max_steps || 10;
  const tLabels = boot.t_labels || [];
  const choices = boot.instrument_choices || [];

  const els = {
    runId: document.getElementById('hypo-run-id'),
    marketBody: document.querySelector('#hypo-market-table tbody'),
    legs: document.getElementById('hypo-legs'),
    addLeg: document.getElementById('hypo-add-leg'),
    nextStep: document.getElementById('hypo-next-step'),
    price: document.getElementById('hypo-price'),
    deleteRun: document.getElementById('hypo-delete-run'),
    loadSample: document.getElementById('hypo-load-sample'),
    toggleScenario: document.getElementById('hypo-toggle-scenario'),
    scenarioPanel: document.getElementById('hypo-scenario-panel'),
    scenarioText: document.getElementById('hypo-scenario-text'),
    applyScenario: document.getElementById('hypo-apply-scenario'),
    scenarioError: document.getElementById('hypo-scenario-error'),
    error: document.getElementById('hypo-error'),
    resultsPanel: document.getElementById('hypo-results-panel'),
    resultsBody: document.querySelector('#hypo-results-table tbody'),
    resultsMeta: document.getElementById('hypo-results-meta'),
  };

  function csrfToken() {
    const input = document.querySelector('[name=csrfmiddlewaretoken]');
    return input ? input.value : '';
  }

  function storageKey(runId) {
    return storagePrefix + (runId || 'HYPO_PORTFOLIO_1').trim();
  }

  function showError(msg) {
    if (!msg) {
      els.error.classList.add('d-none');
      els.error.textContent = '';
      return;
    }
    els.error.textContent = msg;
    els.error.classList.remove('d-none');
  }

  function fmt(n, digits) {
    if (n === null || n === undefined || Number.isNaN(n)) return '—';
    return Number(n).toFixed(digits);
  }

  function tIndex(label) {
    const m = /^t(\d+)$/i.exec(String(label || ''));
    return m ? parseInt(m[1], 10) : 0;
  }

  function lastMarketT() {
    const rows = els.marketBody.querySelectorAll('tr');
    if (!rows.length) return 't1';
    return rows[rows.length - 1].dataset.t || 't1';
  }

  function defaultLeg(slot) {
    return {
      slot: slot,
      code: 'PVE',
      direction: 'long',
      qty: 1,
      active_from: lastMarketT(),
      option_side: 'call',
      strike: 100,
      n_steps: 200,
    };
  }

  function defaultNextStep(prev) {
    const n = tIndex(prev.t) + 1;
    // Manual path: knock ~1M off tenor each step (user edits freely).
    const tau = Math.max(0, Number(prev.tau) - 1.0 / 12.0);
    return {
      t: 't' + n,
      spot: prev.spot,
      vol: prev.vol,
      rate: prev.rate,
      div: prev.div,
      tau: Number(tau.toFixed(8)),
    };
  }

  function syncNextStepButton() {
    const n = els.marketBody.querySelectorAll('tr').length;
    els.nextStep.disabled = n >= maxSteps;
  }

  function readMarketFromTable() {
    const rows = els.marketBody.querySelectorAll('tr');
    return Array.from(rows).map(function (tr) {
      const t = tr.dataset.t;
      const num = function (name) {
        const input = tr.querySelector('[data-m="' + name + '"]');
        return parseFloat(input.value);
      };
      return {
        t: t,
        spot: num('spot'),
        vol: num('vol'),
        rate: num('rate'),
        div: num('div'),
        tau: num('tau'),
      };
    });
  }

  function renderMarket(steps) {
    els.marketBody.innerHTML = '';
    (steps || []).forEach(function (s) {
      const tr = document.createElement('tr');
      tr.dataset.t = s.t;
      tr.innerHTML =
        '<td><strong>' +
        s.t +
        '</strong></td>' +
        ['spot', 'vol', 'rate', 'div', 'tau']
          .map(function (k) {
            return (
              '<td><input class="form-control form-control-sm" type="number" step="any" data-m="' +
              k +
              '" value="' +
              s[k] +
              '"></td>'
            );
          })
          .join('');
      els.marketBody.appendChild(tr);
    });
    syncNextStepButton();
  }

  function needsStrike(code) {
    return code === 'PVE' || code === 'PVA' || code === 'EQF';
  }

  function needsOptionSide(code) {
    return code === 'PVE' || code === 'PVA';
  }

  function needsSteps(code) {
    return code === 'PVA';
  }

  function fromOptionsHtml(selected) {
    const market = readMarketFromTable();
    const labels = market.length
      ? market.map(function (s) {
          return s.t;
        })
      : tLabels.slice(0, 1);
    return labels
      .map(function (t) {
        return (
          '<option value="' +
          t +
          '"' +
          (t === selected ? ' selected' : '') +
          '>' +
          t +
          '</option>'
        );
      })
      .join('');
  }

  function readLegs() {
    const cards = els.legs.querySelectorAll('.nj-hypo-leg');
    return Array.from(cards).map(function (card, i) {
      const code = card.querySelector('[data-f=code]').value;
      const leg = {
        slot: i + 1,
        code: code,
        direction: card.querySelector('[data-f=direction]').value,
        qty: parseFloat(card.querySelector('[data-f=qty]').value),
        active_from: card.querySelector('[data-f=active_from]').value,
      };
      if (needsOptionSide(code)) {
        leg.option_side = card.querySelector('[data-f=option_side]').value;
      }
      if (needsStrike(code)) {
        leg.strike = parseFloat(card.querySelector('[data-f=strike]').value);
      }
      if (needsSteps(code)) {
        leg.n_steps = parseInt(card.querySelector('[data-f=n_steps]').value, 10) || 200;
      }
      return leg;
    });
  }

  function syncLegFields(card) {
    const code = card.querySelector('[data-f=code]').value;
    card.querySelectorAll('[data-show]').forEach(function (el) {
      const want = el.getAttribute('data-show').split(',');
      el.classList.toggle('d-none', want.indexOf(code) === -1);
    });
  }

  function refreshActiveFromOptions() {
    els.legs.querySelectorAll('.nj-hypo-leg').forEach(function (card) {
      const sel = card.querySelector('[data-f=active_from]');
      if (!sel) return;
      const cur = sel.value;
      sel.innerHTML = fromOptionsHtml(cur);
      if (!sel.value && cur) {
        const opt = document.createElement('option');
        opt.value = cur;
        opt.textContent = cur;
        opt.selected = true;
        sel.appendChild(opt);
      }
    });
  }

  function renderLegs(legs) {
    els.legs.innerHTML = '';
    (legs || []).forEach(function (leg, idx) {
      const card = document.createElement('div');
      card.className = 'nj-hypo-leg';
      const opts = choices
        .map(function (c) {
          return (
            '<option value="' +
            c.code +
            '"' +
            (c.code === leg.code ? ' selected' : '') +
            '>' +
            c.label +
            '</option>'
          );
        })
        .join('');
      const fromSel = fromOptionsHtml(leg.active_from || 't1');
      card.innerHTML =
        '<div class="nj-hypo-leg-head">' +
        '<strong>#' +
        (idx + 1) +
        '</strong>' +
        '<button type="button" class="btn btn-sm btn-outline-secondary hypo-remove-leg">Remove</button>' +
        '</div>' +
        '<div class="nj-hypo-leg-fields">' +
        '<label><span>Type</span><select class="form-select form-select-sm" data-f="code">' +
        opts +
        '</select></label>' +
        '<label><span>Direction</span><select class="form-select form-select-sm" data-f="direction">' +
        '<option value="long"' +
        (leg.direction === 'long' ? ' selected' : '') +
        '>long (+)</option>' +
        '<option value="short"' +
        (leg.direction === 'short' ? ' selected' : '') +
        '>short (−)</option>' +
        '</select></label>' +
        '<label><span>Qty</span>' +
        '<input class="form-control form-control-sm" type="number" step="any" min="0" data-f="qty" value="' +
        (leg.qty != null ? leg.qty : 1) +
        '"></label>' +
        '<label><span>From</span><select class="form-select form-select-sm" data-f="active_from">' +
        fromSel +
        '</select></label>' +
        '<label data-show="PVE,PVA"><span>Side</span><select class="form-select form-select-sm" data-f="option_side">' +
        '<option value="call"' +
        (leg.option_side === 'call' ? ' selected' : '') +
        '>call</option>' +
        '<option value="put"' +
        (leg.option_side === 'put' ? ' selected' : '') +
        '>put</option>' +
        '</select></label>' +
        '<label data-show="PVE,PVA,EQF"><span>K</span>' +
        '<input class="form-control form-control-sm" type="number" step="any" data-f="strike" value="' +
        (leg.strike != null ? leg.strike : 100) +
        '"></label>' +
        '<label data-show="PVA"><span>N steps</span>' +
        '<input class="form-control form-control-sm" type="number" step="1" data-f="n_steps" value="' +
        (leg.n_steps != null ? leg.n_steps : 200) +
        '"></label>' +
        '</div>';
      els.legs.appendChild(card);
      syncLegFields(card);
      card.querySelector('[data-f=code]').addEventListener('change', function () {
        syncLegFields(card);
        persist();
      });
      card.querySelector('.hypo-remove-leg').addEventListener('click', function () {
        card.remove();
        renumberLegs();
        persist();
      });
      card.querySelectorAll('input, select').forEach(function (el) {
        el.addEventListener('change', persist);
      });
    });
    els.addLeg.disabled = els.legs.querySelectorAll('.nj-hypo-leg').length >= maxLegs;
  }

  function renumberLegs() {
    els.legs.querySelectorAll('.nj-hypo-leg').forEach(function (card, i) {
      const head = card.querySelector('.nj-hypo-leg-head strong');
      if (head) head.textContent = '#' + (i + 1);
    });
    els.addLeg.disabled = els.legs.querySelectorAll('.nj-hypo-leg').length >= maxLegs;
  }

  function collectState(results) {
    return {
      run_id: els.runId.value.trim() || 'HYPO_PORTFOLIO_1',
      market: readMarketFromTable(),
      instruments: readLegs(),
      results: results || null,
      saved_at: new Date().toISOString(),
    };
  }

  function persist(results) {
    const state = collectState(results);
    try {
      localStorage.setItem(storageKey(state.run_id), JSON.stringify(state));
    } catch (e) {
      /* quota / private mode */
    }
  }

  function loadStateForRun(runId) {
    try {
      const raw = localStorage.getItem(storageKey(runId));
      if (!raw) return null;
      return JSON.parse(raw);
    } catch (e) {
      return null;
    }
  }

  function renderResults(payload) {
    if (!payload || !payload.steps) {
      els.resultsPanel.classList.add('d-none');
      return;
    }
    els.resultsPanel.classList.remove('d-none');
    els.resultsMeta.textContent = payload.run_id || '';
    els.resultsBody.innerHTML = '';

    payload.steps.forEach(function (step) {
      (step.legs || []).forEach(function (leg) {
        const tr = document.createElement('tr');
        if (leg.active === false) tr.classList.add('nj-hypo-inactive');
        const label =
          '#' +
          leg.slot +
          ' ' +
          leg.code +
          ' ' +
          leg.direction +
          ' ×' +
          fmt(leg.qty, 4) +
          (leg.option_side ? ' ' + leg.option_side : '') +
          (leg.active === false ? ' (off)' : '');
        tr.innerHTML =
          '<td>' +
          step.t +
          '</td><td>' +
          label +
          '</td><td>' +
          fmt(leg.npv, 4) +
          '</td><td>' +
          fmt(leg.d_npv, 4) +
          '</td><td>' +
          fmt(leg.delta, 4) +
          '</td><td>' +
          fmt(leg.gamma, 6) +
          '</td><td>' +
          fmt(leg.vega, 4) +
          '</td><td>' +
          fmt(leg.theta_day, 4) +
          '</td><td>' +
          fmt(leg.rho, 4) +
          '</td>';
        els.resultsBody.appendChild(tr);
      });
      const trPort = document.createElement('tr');
      trPort.className = 'nj-hypo-port-row';
      trPort.innerHTML =
        '<td>' +
        step.t +
        '</td><td><strong>Portfolio</strong></td><td><strong>' +
        fmt(step.portfolio_npv, 4) +
        '</strong></td><td><strong>' +
        fmt(step.portfolio_d_npv, 4) +
        '</strong></td><td colspan="5"></td>';
      els.resultsBody.appendChild(trPort);
    });
  }

  function applyBootOrStorage() {
    const runId = els.runId.value.trim() || 'HYPO_PORTFOLIO_1';
    const saved = loadStateForRun(runId);
    if (saved) {
      const market = saved.market && saved.market.length ? saved.market : boot.default_market;
      renderMarket(market);
      const legs = (saved.instruments || [])
        .filter(function (ins) {
          return ins.code !== 'IXS';
        })
        .map(function (ins) {
          return Object.assign({ qty: 1, active_from: 't1' }, ins);
        });
      renderLegs(legs.length ? legs : [defaultLeg(1)]);
      if (saved.results) renderResults(saved.results);
      return;
    }
    renderMarket(boot.default_market);
    renderLegs([defaultLeg(1)]);
    renderResults(null);
  }

  els.addLeg.addEventListener('click', function () {
    const legs = readLegs();
    if (legs.length >= maxLegs) return;
    legs.push(defaultLeg(legs.length + 1));
    renderLegs(legs);
    persist();
  });

  els.nextStep.addEventListener('click', function () {
    showError('');
    const market = readMarketFromTable();
    if (!market.length) {
      renderMarket(boot.default_market);
      persist();
      return;
    }
    if (market.length >= maxSteps) {
      showError('Already at t' + maxSteps + '.');
      return;
    }
    const last = market[market.length - 1];
    const nextIdx = tIndex(last.t) + 1;
    if (nextIdx > maxSteps) {
      showError('Already at t' + maxSteps + '.');
      return;
    }
    market.push(defaultNextStep(last));
    renderMarket(market);
    refreshActiveFromOptions();
    persist();
  });

  els.runId.addEventListener('change', function () {
    applyBootOrStorage();
  });

  els.price.addEventListener('click', async function () {
    showError('');
    const body = {
      run_id: els.runId.value.trim() || 'HYPO_PORTFOLIO_1',
      market: readMarketFromTable(),
      instruments: readLegs(),
    };
    if (!body.market.length) {
      showError('Add at least one market step.');
      return;
    }
    if (!body.instruments.length) {
      showError('Add at least one instrument.');
      return;
    }
    els.price.disabled = true;
    try {
      const res = await fetch(priceUrl, {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
          'X-CSRFToken': csrfToken(),
        },
        body: JSON.stringify(body),
      });
      const data = await res.json();
      if (!res.ok || !data.ok) {
        showError(data.error || 'Pricing failed.');
        return;
      }
      renderResults(data);
      persist(data);
    } catch (e) {
      showError(String(e));
    } finally {
      els.price.disabled = false;
    }
  });

  els.deleteRun.addEventListener('click', function () {
    const runId = els.runId.value.trim() || 'HYPO_PORTFOLIO_1';
    if (!window.confirm('Delete run ' + runId + ' from this browser?')) return;
    try {
      localStorage.removeItem(storageKey(runId));
    } catch (e) {
      /* ignore */
    }
    renderMarket(boot.default_market);
    renderLegs([defaultLeg(1)]);
    renderResults(null);
    showError('');
  });

  els.loadSample.addEventListener('click', function () {
    const a = document.createElement('a');
    a.href = sampleUrl;
    a.download = 'hypo_market_sample.json';
    a.click();
  });

  els.toggleScenario.addEventListener('click', function () {
    const open = els.scenarioPanel.classList.toggle('d-none');
    if (!open) {
      els.scenarioText.value = boot.sample_market_json || '';
      els.scenarioError.classList.add('d-none');
    }
  });

  els.applyScenario.addEventListener('click', function () {
    els.scenarioError.classList.add('d-none');
    let parsed;
    try {
      parsed = JSON.parse(els.scenarioText.value);
    } catch (e) {
      els.scenarioError.textContent = 'Invalid JSON: ' + e.message;
      els.scenarioError.classList.remove('d-none');
      return;
    }
    const steps = parsed.steps;
    if (!Array.isArray(steps) || !steps.length) {
      els.scenarioError.textContent = 'JSON needs a non-empty "steps" array.';
      els.scenarioError.classList.remove('d-none');
      return;
    }
    if (steps.length > maxSteps) {
      els.scenarioError.textContent = 'At most ' + maxSteps + ' steps.';
      els.scenarioError.classList.remove('d-none');
      return;
    }
    const byT = {};
    let bad = null;
    steps.forEach(function (s) {
      if (!s || !s.t) {
        bad = 'Each step needs a "t" label (t1…t' + maxSteps + ').';
        return;
      }
      const t = String(s.t).toLowerCase();
      const idx = tIndex(t);
      if (idx < 1 || idx > maxSteps) {
        bad = 'Invalid step label: ' + s.t;
        return;
      }
      byT[t] = Object.assign({}, s, { t: t });
    });
    if (bad) {
      els.scenarioError.textContent = bad;
      els.scenarioError.classList.remove('d-none');
      return;
    }
    const ordered = Object.keys(byT)
      .sort(function (a, b) {
        return tIndex(a) - tIndex(b);
      })
      .map(function (t) {
        return byT[t];
      });
    renderMarket(ordered);
    refreshActiveFromOptions();
    persist();
    els.scenarioPanel.classList.add('d-none');
  });

  els.marketBody.addEventListener('change', function () {
    persist();
  });

  applyBootOrStorage();
})();
