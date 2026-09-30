#!/usr/bin/env python3
"""Real Chromium UI tests, with simulated HTTP responses (not an ESP32)."""
import copy
import json
import os
import shutil
from types import SimpleNamespace
from pathlib import Path
from urllib.parse import urlparse, parse_qs
from playwright.sync_api import sync_playwright

ROOT = Path(__file__).resolve().parents[1]
defaults = json.loads((ROOT/'tests/fixtures/config.json').read_text())
catalog = (ROOT/'tests/fixtures/catalog.json').read_text()
state = copy.deepcopy(defaults)
requests = []
errors = []
checks = 0

def check(condition, name):
    global checks
    assert condition, name
    checks += 1
    print('PASS', name, flush=True)

def handler(request):
    global state
    path = urlparse(request.url).path
    requests.append((request.method, path))
    data, mime, code = '', 'application/json', 200
    if path in ['/', '/app.css', '/app.js']:
        name, mime = {'/':('index.html','text/html'), '/app.css':('app.css','text/css'), '/app.js':('app.js','application/javascript')}[path]
        data = (ROOT/'web'/name).read_text()
    elif path == '/api/config':
        data = json.dumps(state)
    elif path == '/api/catalog':
        data = catalog
    elif path == '/api/save':
        form = {k:v[0] for k,v in parse_qs(request.post_data).items()}
        assert request.headers.get('x-setup-token') == state['token']
        assert int(form['revision']) == state['revision']
        index = int(form['profile'])
        actions = []
        for i in range(6):
            a = {field:int(form[f'b{i}_{field}']) for field in ['kind','usage','modifiers','repeat']}
            a['label'] = form[f'b{i}_label']
            actions.append(a)
        state['profiles'][index]['actions'] = actions
        state['revision'] += 1
        state['storage'] = 'Saved on device'
        data = json.dumps(state)
    elif path == '/api/reset':
        form = {k:v[0] for k,v in parse_qs(request.post_data).items()}
        index = int(form['profile'])
        if index == 3:
            state['profiles'] = copy.deepcopy(defaults['profiles'])
        else:
            state['profiles'][index] = copy.deepcopy(defaults['profiles'][index])
        state['revision'] += 1
        data = json.dumps(state)
    elif path == '/api/exit':
        data = '{"ok":true}'
    else:
        code = 404
        data = '{"error":"Not found"}'
    return {'status':code, 'mime':mime, 'body':data}

with sync_playwright() as p:
    binary = os.environ.get('CHROMIUM_PATH') or shutil.which('chromium')
    kwargs = {'headless':True, 'args':['--no-sandbox']}
    if binary:
        kwargs['executable_path'] = binary
    browser = p.chromium.launch(**kwargs)
    page = browser.new_page(viewport={'width':1280,'height':960}, device_scale_factor=1)
    page.on('pageerror', lambda e: errors.append(str(e)))
    # The sandbox browser blocks URL navigation. Render only local strings and
    # bridge fetch to the simulated API; no external network or browser policy changes.
    def bridge(source, path, options):
        return handler(SimpleNamespace(url=path,method=options.get('method','GET'),
             headers={k.lower():v for k,v in options.get('headers',{}).items()},post_data=options.get('body','')))
    page.expose_binding('_localApi', bridge)
    html = (ROOT/'web/index.html').read_text().replace('<link rel="stylesheet" href="/app.css"><script src="/app.js" defer></script>','')
    page.set_content(html)
    page.add_style_tag(content=(ROOT/'web/app.css').read_text())
    page.evaluate("""() => { window.fetch = async (path, options = {}) => {
      const data = await window._localApi(String(path), {method: options.method || 'GET', headers: options.headers || {}, body: options.body || ''});
      return new Response(data.body, {status: data.status, headers: {'Content-Type': data.mime}});
    }; }""")
    page.add_script_tag(content=(ROOT/'web/app.js').read_text())
    page.wait_for_selector('#fields:not([disabled])')
    check(page.locator('.card').count()==6, 'all six editable key cards render')
    check(page.locator('#profile option').count()==3, 'exactly three profiles in browser')
    check(page.get_by_label('Up command', exact=True).input_value()=='182', 'phone default Up is Previous track')
    check(page.get_by_label('Left command', exact=True).input_value()=='233', 'phone default Left is Volume +')
    check(page.get_by_label('A repeat', exact=True).is_disabled() and page.get_by_label('B repeat', exact=True).is_disabled(), 'A and B repeat controls stay locked')
    check(page.evaluate('document.documentElement.scrollWidth<=innerWidth'), 'desktop layout has no horizontal overflow')
    (ROOT/'tests/browser').mkdir(exist_ok=True)
    page.screenshot(path=str(ROOT/'tests/browser/desktop.png'), full_page=True)
    page.select_option('#profile','1')
    page.get_by_label('A command',exact=True).select_option('6')
    page.get_by_label('A Ctrl',exact=True).check()
    page.get_by_label('A Shift',exact=True).check()
    page.get_by_label('A OLED label',exact=True).fill('Copy')
    check('Unsaved' in page.locator('#dirty').inner_text(), 'edits visibly mark the selected profile unsaved')
    page.click('#save')
    page.wait_for_function('document.getElementById("message").textContent.includes("Saved on device")')
    check(state['profiles'][1]['actions'][4]['usage']==6 and state['profiles'][1]['actions'][4]['modifiers']==3, 'save sends keyboard usage and Ctrl+Shift bits')
    check(state['profiles'][0]['actions']==defaults['profiles'][0]['actions'] and state['profiles'][2]['actions']==defaults['profiles'][2]['actions'], 'save leaves both other profiles unchanged')
    page.get_by_label('A OLED label',exact=True).fill('中文')
    count = len([r for r in requests if r[1]=='/api/save'])
    page.click('#save')
    check('ASCII' in page.locator('#message').inner_text() and len([r for r in requests if r[1]=='/api/save'])==count, 'client rejects a non-ASCII OLED label before HTTP save')
    page.get_by_label('A OLED label',exact=True).fill('New label')
    page.evaluate('window.confirm = () => false')
    page.select_option('#profile','0')
    check(page.locator('#profile').input_value()=='1', 'canceling discard keeps the edited profile selected')
    page.evaluate('window.confirm = () => true')
    page.select_option('#profile','0')
    check(page.get_by_label('A command',exact=True).input_value()=='205', 'accepting discard loads the other profile')
    page.get_by_label('Up action type',exact=True).select_option('0')
    check(page.get_by_label('Up command',exact=True).is_disabled() and page.get_by_label('Up repeat',exact=True).is_disabled(), 'disabled action locks command and repeat inputs')
    page.click('#save')
    page.wait_for_function('document.getElementById("message").textContent.includes("Saved on device")')
    check(state['profiles'][0]['actions'][0]['kind']==0 and state['profiles'][0]['actions'][0]['usage']==0, 'disabled action serializes canonical zero usage')
    page.evaluate('window.confirm = () => true')
    page.click('#reset')
    page.wait_for_function('document.getElementById("message").textContent.includes("Factory defaults")')
    check(state['profiles'][0]['actions']==defaults['profiles'][0]['actions'], 'Reset this profile restores its factory values')
    page.set_viewport_size({'width':390,'height':844})
    check(page.evaluate('document.documentElement.scrollWidth<=innerWidth'), '390px mobile layout has no horizontal overflow')
    check(page.locator('.card').first.bounding_box()['width']>300, 'mobile uses full-width single-column key cards')
    page.screenshot(path=str(ROOT/'tests/browser/mobile.png'), full_page=True)
    page.evaluate('window.confirm = () => true')
    page.click('#resetAll')
    page.wait_for_function('document.getElementById("message").textContent.includes("Factory defaults")')
    check(state['profiles']==defaults['profiles'], 'Restore all resets the three profiles')
    page.click('#exit')
    page.wait_for_function('document.getElementById("message").textContent.includes("Setup is closing")')
    check(page.locator('#save').is_disabled() and page.locator('#profile').is_disabled(), 'exit locks the browser controls')
    check(not errors, 'no browser JavaScript execution errors')
    check(all(path in ['/','/app.js','/app.css','/api/config','/api/catalog','/api/save','/api/reset','/api/exit','/favicon.ico'] for _,path in requests), 'page requests only local firmware assets and APIs')
    browser.close()
print(f'\n{checks} Chromium UI checks passed against simulated HTTP responses.')
print('Local HTML/CSS/JS rendered in memory; fetch and confirmation responses are test doubles. No real HTTP/CSP, Wi-Fi, iOS or ESP32 tested.')
