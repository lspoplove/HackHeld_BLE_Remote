'use strict';
(() => {
  const $ = id => document.getElementById(id);
  const buttons = ['Up', 'Down', 'Left', 'Right', 'A', 'B'];
  const symbols = ['↑', '↓', '←', '→', 'A', 'B'];
  let state = null, catalog = null, draft = [], selected = 0;
  let changed = false, busy = false, exited = false, rendering = false;
  const clone = obj => JSON.parse(JSON.stringify(obj));
  function message(text, style = '') { $('message').textContent = text; $('message').className = 'message ' + style; }
  function controls() {
    const blocked = busy || !state || !catalog || exited;
    for (const id of ['profile', 'fields', 'save', 'reset', 'resetAll', 'exit']) $(id).disabled = blocked;
    $('reload').disabled = busy || exited;
    $('dirty').textContent = changed ? 'Unsaved changes in this profile' : 'No unsaved changes';
    $('dirty').className = 'dirty' + (changed ? ' changed' : '');
  }
  async function request(path, form) {
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), 12000);
    try {
      const opts = {cache: 'no-store', signal: controller.signal};
      if (form !== undefined) {
        opts.method = 'POST'; opts.headers = {'Content-Type': 'application/x-www-form-urlencoded', 'X-Setup-Token': state.token};
        opts.body = form.toString();
      }
      const response = await fetch(path, opts);
      let data;
      try { data = await response.json(); } catch (_) { throw new Error('Unexpected response. Stay connected to the setup Wi-Fi and open http://192.168.4.1.'); }
      if (!response.ok) throw new Error(data.error || 'Device request failed.');
      return data;
    } finally { clearTimeout(timer); }
  }
  function validateState(value) {
    if (!value || value.version !== 3 || !Array.isArray(value.profiles) || value.profiles.length !== 3 ||
        typeof value.token !== 'string' || !Number.isInteger(value.revision) ||
        value.profiles.some(p => !Array.isArray(p.actions) || p.actions.length !== 6))
      throw new Error('Unexpected firmware configuration. Check that the device runs v3.');
    return value;
  }
  function option(select, value, text) { const el = document.createElement('option'); el.value = value; el.textContent = text; select.append(el); }
  function commandOptions(select, action) {
    select.replaceChildren();
    const choices = action.kind === 1 ? catalog.keyboard : action.kind === 2 ? catalog.media : [{usage: 0, name: 'No action'}];
    for (const key of choices) option(select, key.usage, key.name);
    select.value = String(action.usage); select.disabled = action.kind === 0;
  }
  function render() {
    // Removing a focused input can synchronously emit change/blur from the old
    // profile. Suppress those events before swapping the DOM and active draft.
    rendering = true;
    $('keys').replaceChildren();
    $('storage').textContent = state.storage;
    draft.forEach((action, index) => {
      const card = document.createElement('section'); card.className = 'card'; card.dataset.index = String(index);
      // Only constant markup enters innerHTML. User labels go through text/value properties.
      card.innerHTML = '<div class="card-top"><span class="keycap"></span><div><h2></h2><div class="sub"></div></div></div>' +
        '<label>Action type<select data-field="kind"></select></label><label>Command<select data-field="usage"></select></label>' +
        '<div class="modifiers"></div><label>OLED label<input data-field="label" type="text" maxlength="12" required autocomplete="off" spellcheck="false"></label>' +
        '<label class="check repeat"><input data-field="repeat" type="checkbox">Repeat while held</label>';
      card.querySelector('.keycap').textContent = symbols[index];
      card.querySelector('h2').textContent = buttons[index];
      card.querySelector('.sub').textContent = index < 4 ? 'Runs on press' : 'Runs on release · A+B reserved';
      const kind = card.querySelector('[data-field=kind]');
      option(kind, 2, 'Media control'); option(kind, 1, 'Keyboard shortcut'); option(kind, 0, 'Disabled'); kind.value = String(action.kind);
      kind.setAttribute('aria-label', buttons[index] + ' action type');
      const usage = card.querySelector('[data-field=usage]'); usage.setAttribute('aria-label', buttons[index] + ' command'); commandOptions(usage, action);
      const mods = card.querySelector('.modifiers');
      ['Ctrl', 'Shift', 'Alt', 'Win/Cmd'].forEach((name, bit) => {
        const lab = document.createElement('label'); lab.className = 'check';
        const check = document.createElement('input'); check.type = 'checkbox'; check.dataset.mod = String(1 << bit);
        check.checked = Boolean(action.modifiers & (1 << bit)); check.disabled = action.kind !== 1;
        check.setAttribute('aria-label', buttons[index] + ' ' + name); lab.append(check, document.createTextNode(name)); mods.append(lab);
      });
      const label = card.querySelector('[data-field=label]'); label.value = action.label; label.setAttribute('aria-label', buttons[index] + ' OLED label');
      const repeat = card.querySelector('[data-field=repeat]'); repeat.checked = Boolean(action.repeat); repeat.disabled = index >= 4 || action.kind === 0;
      repeat.setAttribute('aria-label', buttons[index] + ' repeat'); $('keys').append(card);
    });
    controls(); rendering = false;
  }
  function autoLabel(action) {
    if (action.kind === 0) return 'Disabled';
    const list = action.kind === 1 ? catalog.keyboard : catalog.media;
    const key = list.find(k => k.usage === action.usage);
    const mods = action.kind === 1 ? ['C+', 'S+', 'A+', 'G+'].filter((_, bit) => action.modifiers & (1 << bit)).join('') : '';
    return (mods + (key ? key.name : 'Key')).slice(0, 12);
  }
  function readCard(card) {
    const index = Number(card.dataset.index);
    const kind = Number(card.querySelector('[data-field=kind]').value);
    const action = {kind, usage: Number(card.querySelector('[data-field=usage]').value), modifiers: 0, repeat: 0,
      label: card.querySelector('[data-field=label]').value};
    if (kind === 1) for (const check of card.querySelectorAll('[data-mod]')) if (check.checked) action.modifiers |= Number(check.dataset.mod);
    if (kind !== 0 && index < 4 && card.querySelector('[data-field=repeat]').checked) action.repeat = 1;
    if (kind === 0) action.usage = 0;
    draft[index] = action; return action;
  }
  $('keys').addEventListener('change', event => {
    const card = event.target.closest('.card');
    if (rendering || !card || !$('keys').contains(card)) return;
    const index = Number(card.dataset.index);
    if (event.target.dataset.field === 'kind') {
      const kind = Number(event.target.value);
      draft[index] = {kind, usage: kind === 1 ? 40 : kind === 2 ? 205 : 0, modifiers: 0, repeat: 0, label: ''};
      draft[index].label = autoLabel(draft[index]); changed = true; render();
    } else {
      const action = readCard(card);
      if (event.target.dataset.field === 'usage' || event.target.dataset.mod) {
        action.label = autoLabel(action); card.querySelector('[data-field=label]').value = action.label;
      }
      changed = true; controls();
    }
  });
  $('keys').addEventListener('input', event => {
    if (rendering || !$('keys').contains(event.target)) return;
    if (event.target.dataset.field === 'label') { readCard(event.target.closest('.card')); changed = true; controls(); }
  });
  $('profile').addEventListener('change', () => {
    if (changed && !confirm('Discard unsaved changes in this profile?')) { $('profile').value = String(selected); return; }
    selected = Number($('profile').value); draft = clone(state.profiles[selected].actions); changed = false; render(); message('Editing ' + state.profiles[selected].name + '.');
  });
  async function load() {
    if (busy || exited) return;
    if (changed && !confirm('Discard unsaved changes and reload from the device?')) return;
    busy = true; controls(); message('Loading configuration…');
    try {
      const data = await request('/api/config');
      const choices = await request('/api/catalog');
      state = validateState(data); catalog = choices; draft = clone(state.profiles[selected].actions); changed = false;
      render(); message('Connected. Changes are stored only when you press Save.', 'success');
    } catch (error) { message(error.name === 'AbortError' ? 'Request timed out. Check the setup Wi-Fi and retry.' : error.message, 'error'); }
    finally { busy = false; controls(); }
  }
  async function write(path, form, success) {
    if (busy || exited) return;
    busy = true; controls(); message('Saving to the device…');
    try {
      state = validateState(await request(path, form)); draft = clone(state.profiles[selected].actions); changed = false;
      render(); message(success, 'success');
    } catch (error) {
      const text = error.name === 'AbortError' ? 'Request timed out.' : error.message;
      message(text + ' Save is not confirmed; reload to check the device before retrying.', 'error');
    } finally { busy = false; controls(); }
  }
  $('editor').addEventListener('submit', event => {
    event.preventDefault(); if (!state || busy || exited) return;
    for (const card of $('keys').querySelectorAll('.card')) readCard(card);
    if (draft.some(a => !/^[\x20-\x7e]{1,12}$/.test(a.label))) { message('Every label needs 1–12 printable English/ASCII characters.', 'error'); return; }
    const form = new URLSearchParams({profile: selected, revision: state.revision});
    draft.forEach((a, i) => { for (const field of ['kind', 'usage', 'modifiers', 'repeat', 'label']) form.set('b' + i + '_' + field, a[field]); form.set('b' + i + '_button', i); });
    write('/api/save', form, 'Saved on device. Exit setup to use this profile.');
  });
  $('reload').addEventListener('click', load);
  function reset(all) {
    if (!state || busy || exited || !confirm(all ? 'Restore and save factory defaults for ALL THREE profiles? This also discards unsaved edits.' : 'Restore and save factory defaults for this profile?')) return;
    write('/api/reset', new URLSearchParams({profile: all ? 3 : selected, revision: state.revision}), 'Factory defaults saved on device.');
  }
  $('reset').addEventListener('click', () => reset(false)); $('resetAll').addEventListener('click', () => reset(true));
  $('exit').addEventListener('click', async () => {
    if (!state || busy || exited || (changed && !confirm('Exit without saving these edits?'))) return;
    busy = true; controls();
    try { await request('/api/exit', new URLSearchParams()); exited = true; changed = false; message('Setup is closing. Choose Phone or Computer on your remote.', 'success'); }
    catch (error) { message('Exit was not confirmed. Press B on the remote to close setup.', 'error'); }
    finally { busy = false; controls(); }
  });
  window.addEventListener('beforeunload', event => { if (changed && !exited) { event.preventDefault(); event.returnValue = ''; } });
  load();
})();
