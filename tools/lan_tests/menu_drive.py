"""menu_drive.py (findings/68 s.6l) <tcp port> <host|guest> <log file> <the game's log> -- drives one copy's menus
through the debug console like a player with a controller: skips the intro and
the title, opens Extras -> Multiplayer, hosts (host) or finds the host's game
and joins it (guest), goes back, loads the first save, and checks the session.
Every step is written to the log file (with the photos' paths)."""
import re, socket, sys, time

port, role, log_path, game_log = int(sys.argv[1]), sys.argv[2], sys.argv[3], sys.argv[4]
log = open(log_path, 'w', buffering=1)
def say(*a):
    log.write(time.strftime('%H:%M:%S ') + ' '.join(str(x) for x in a) + '\n')

def connect():
    deadline = time.time() + 600
    while True:
        try:
            s = socket.create_connection(('127.0.0.1', port))
            return s, s.makefile('r', encoding='utf-8', errors='replace')
        except OSError:
            if time.time() > deadline: raise
            time.sleep(0.5)
sock, reader = connect()

def cmd(line):
    sock.sendall(line.encode() + b'\n')
    out = []
    while True:
        r = reader.readline()
        if not r: raise SystemExit('the game closed the connection')
        r = r.rstrip('\n')
        if r.startswith('@end '):
            return '\n'.join(out), r[5:] == 'ok'
        out.append(r)

def state():
    t, _ = cmd('frontend')
    m = re.search(r'state (-?\d+)', t)
    return int(m.group(1)) if m else -1

def press(b, after=0.45):
    cmd(b)
    time.sleep(after)

def photo(what):
    t, ok = cmd('photo')
    say('photo', what, t.splitlines()[0] if t else '(none)')

def fail(why):
    say('FAILED:', why)
    photo('at the failure')
    raise SystemExit(1)

def game_log_size():
    try:
        with open(game_log, 'rb') as f:
            f.seek(0, 2); return f.tell()
    except OSError:
        return 0
def game_log_since(at):
    try:
        with open(game_log, 'rb') as f:
            f.seek(at); return f.read().decode('utf-8', 'replace')
    except OSError:
        return ''
def expect(at, text, seconds=3.0):
    """Waits until the game's log has `text` after position `at`."""
    t0 = time.time()
    while time.time() - t0 < seconds:
        if text in game_log_since(at): return True
        time.sleep(0.2)
    return False

MAIN = {66, 68}
def fe():
    return cmd('frontend')[0]
def main_menu():
    """(set, item, input) from 'main menu: set first, item 3, input on', or None."""
    m = re.search(r'main menu: set (\w+), item (\d+), input (\w+)', fe())
    return (m.group(1), int(m.group(2)), m.group(3) == 'on') if m else None
def options_row():
    """(tab, row) from 'options: tab Multiplayer, row Host a Game', or None (closed)."""
    m = re.search(r'options: tab ([^,\n]*), row ([^\n]*)', fe())
    return (m.group(1), m.group(2).strip()) if m else None
def wait_for(test, seconds, step=0.2):
    t0 = time.time()
    while time.time() - t0 < seconds:
        v = test()
        if v: return v
        time.sleep(step)
    return None

def menu_input_on(seconds=10):
    return wait_for(lambda: (lambda m: m and m[2])(main_menu()), seconds)

def main_menu_to(target, set_name):
    """Moves the main menu's cursor to item `target` one press at a time, each
    press checked (the selection changed) before the next."""
    for _ in range(30):
        m = main_menu()
        if not m: fail('no main menu: ' + fe())
        if m[0] != set_name: fail('main menu shows set %s, not %s' % (m[0], set_name))
        if m[1] == target: return
        if not m[2]:
            time.sleep(0.3); continue
        before = m[1]
        press('down' if target > before else 'up', 0.1)
        if not wait_for(lambda: (lambda n: n and n[1] != before)(main_menu()), 1.5):
            say('a press did not move the cursor (item %d), again' % before)
    fail('could not move the main menu to item %d' % target)

def options_to(label):
    for _ in range(12):
        r = options_row()
        if not r: fail('the Options screen is not open: ' + fe())
        if r[1] == label: return
        before = r[1]
        press('down', 0.1)
        if not wait_for(lambda: (lambda n: n and n[1] != before)(options_row()), 1.5):
            say('a press did not move the Options cursor (%s), again' % before)
    fail('no row "%s" on the Options screen' % label)

# 1. The intro movies and the title: START until the main menu.
t0 = time.time()
seen = []
while True:
    s = state()
    if not seen or seen[-1] != s:
        seen.append(s); say('front end state', s)
    if s in MAIN and main_menu(): break
    if time.time() - t0 > 120: fail('no main menu in 120 s (states %s)' % seen)
    if s >= 50 and s not in MAIN:  # movies / title
        press('start', 1.2)
    else:
        time.sleep(0.5)
if not menu_input_on(15): fail('the main menu never took input: ' + fe())
say(fe().replace('\n', ' | '))
photo('main menu')
# 2. Extras -> Multiplayer (the Options screen on its Multiplayer tab).
main_menu_to(3, 'first')
at = game_log_size()
press('a', 0.2)
if not wait_for(lambda: (lambda m: m and m[0] == 'extras')(main_menu()), 3):
    fail('A on Extras did not show the Extras set: ' + game_log_since(at)[-300:])
if not menu_input_on(5): fail('the Extras set never took input')
main_menu_to(5, 'extras')
photo('extras set')
at = game_log_size()
press('a', 0.2)
if not wait_for(lambda: (lambda r: r and r[0] == 'Multiplayer')(options_row()), 6):
    fail('the Multiplayer item did not open the tab: ' + fe() + ' | ' + game_log_since(at)[-300:])
time.sleep(1.5)  # the screen slides in
say(fe().replace('\n', ' | '))
photo('multiplayer tab')
if role == 'host':
    options_to('Host a Game')
    at = game_log_size()
    press('a', 1.5)
    say('net:', cmd('net')[0])
    photo('hosting')
    t0 = time.time()
    while True:
        n = cmd('net')[0]
        if 'connected' in n and 'waiting' not in n: break
        if time.time() - t0 > 120: fail('nobody joined in 120 s: ' + n)
        time.sleep(1)
    time.sleep(4.0)  # the input delay is picked from 4 pings
    say('net:', cmd('net')[0])
    photo('player 2 joined')
else:
    t0 = time.time()
    while 'found a game' not in game_log_since(0):
        if time.time() - t0 > 90: fail('no game found on the network in 90 s')
        time.sleep(0.5)
    time.sleep(1.5)
    options_to('Game')
    photo('games list')
    options_to('Join')
    at = game_log_size()
    press('a', 2.0)
    if not expect(at, 'Online: Join', 3.0):
        fail('Join was not pressed: ' + game_log_since(at)[-300:])
    say('net:', cmd('net')[0])
    photo('joined')
    t0 = time.time()
    while 'connected' not in cmd('net')[0]:
        if time.time() - t0 > 30: fail('not connected in 30 s: ' + cmd('net')[0])
        time.sleep(1)
    time.sleep(4.0)
    say('net:', cmd('net')[0])
# 3. Back to the main menu, Load Game, the first save.
for _ in range(5):
    if not options_row(): break
    press('b', 1.5)
if options_row(): fail('the Options screen did not close: ' + fe())
if not menu_input_on(8): fail('back on the main menu: no input: ' + fe())
say(fe().replace('\n', ' | '))
m = main_menu()
if m and m[0] == 'extras':
    press('b', 0.2)
    if not wait_for(lambda: (lambda n: n and n[0] == 'first')(main_menu()), 3): fail('B did not go back to the first set')
    menu_input_on(5)
main_menu_to(1, 'first')
photo('load game picked')
press('a', 4.0)
photo('save list')
t0 = time.time()
seen = []
while True:
    s = state()
    if not seen or seen[-1] != s:
        seen.append(s); say('front end state', s)
    if s in (486, 489): break  # in the level
    if time.time() - t0 > 120: fail('no level in 120 s (states %s)' % seen)
    if s in (282, 160, 159):  # Load successful / the list: A
        press('a', 1.5)
    else:
        time.sleep(0.5)
say('in the level')
time.sleep(4)
if role == 'guest':
    # Player 2 drops in with START on the guest's own controller.
    for _ in range(3):
        press('start 300', 4.0)
        p = cmd('players')[0]
        if 'p2:' in p: break
    say('players:', cmd('players')[0])
    if 'p2:' not in cmd('players')[0]: fail('player 2 did not drop in')
    # A little walking and attacking by player 2.
    for b in ['lsup 1500', 'x', 'x', 'lsright 1000', 'a', 'x']:
        press(b, 1.2)
time.sleep(20)
say('net:', cmd('net')[0])
photo('playing')
say('players:', cmd('players')[0])
say('DONE')
