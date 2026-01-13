import curses
import selectors
import socket
import struct
import sys
import time

import actions_pb2
import cards_pb2
import events_pb2
import response_pb2


class FrameBuffer:
    def __init__(self):
        self.buf = bytearray()

    def feed(self, data: bytes):
        self.buf.extend(data)
        frames = []
        while True:
            if len(self.buf) < 4:
                break
            (size,) = struct.unpack("!I", self.buf[:4])
            if len(self.buf) < 4 + size:
                break
            frames.append(bytes(self.buf[4 : 4 + size]))
            del self.buf[: 4 + size]
        return frames


def frame_message(payload: bytes) -> bytes:
    return struct.pack("!I", len(payload)) + payload


RANK_LABELS = {
    cards_pb2.RANK_TWO: "2",
    cards_pb2.RANK_THREE: "3",
    cards_pb2.RANK_FOUR: "4",
    cards_pb2.RANK_FIVE: "5",
    cards_pb2.RANK_SIX: "6",
    cards_pb2.RANK_SEVEN: "7",
    cards_pb2.RANK_EIGHT: "8",
    cards_pb2.RANK_NINE: "9",
    cards_pb2.RANK_TEN: "10",
    cards_pb2.RANK_JACK: "J",
    cards_pb2.RANK_QUEEN: "Q",
    cards_pb2.RANK_KING: "K",
    cards_pb2.RANK_ACE: "A",
}

SUIT_SYMBOLS = {
    cards_pb2.SUIT_SPADES: "\u2660",
    cards_pb2.SUIT_HEARTS: "\u2665",
    cards_pb2.SUIT_DIAMONDS: "\u2666",
    cards_pb2.SUIT_CLUBS: "\u2663",
}

PHASE_LABELS = {
    events_pb2.Event.PHASE_UNSPECIFIED: "?",
    events_pb2.Event.PHASE_HOLDING: "holding",
    events_pb2.Event.PHASE_PREFLOP: "preflop",
    events_pb2.Event.PHASE_FLOP: "flop",
    events_pb2.Event.PHASE_TURN: "turn",
    events_pb2.Event.PHASE_RIVER: "river",
    events_pb2.Event.PHASE_SHOWDOWN: "showdown",
}


def action_to_str(action: actions_pb2.Action) -> str:
    payload = action.WhichOneof("payload")
    if payload == "fold":
        return "fold"
    if payload == "bet":
        return f"bet {action.bet.amount}"
    return "unknown"


class ClientState:
    def __init__(self):
        self.player_id = None
        self.phase = events_pb2.Event.PHASE_HOLDING
        self.board = []
        self.hole = []
        self.showdown = {}
        self.players = {}
        self.active_bets = {}
        self.turn = None
        self.pot = 0
        self.logs = []
        self.hand_active = False

    def log(self, msg: str):
        timestamp = time.strftime("%H:%M:%S")
        self.logs.append(f"{timestamp} {msg}")
        self.logs = self.logs[-6:]

    def reset_hand(self):
        self.phase = events_pb2.Event.PHASE_HOLDING
        self.board = []
        self.hole = []
        self.showdown = {}
        self.active_bets = {}
        self.turn = None
        self.pot = 0
        self.hand_active = True

    def call_amount(self) -> int:
        if self.player_id is None:
            return 0
        max_bet = max(self.active_bets.values(), default=0)
        current = self.active_bets.get(self.player_id, 0)
        return max(0, max_bet - current)

    def player_chips(self) -> int:
        if self.player_id is None:
            return 0
        return self.players.get(self.player_id, 0)

    def apply_event(self, ev: events_pb2.Event):
        payload = ev.WhichOneof("payload")
        if payload == "player_added":
            who = ev.player_added.who
            self.players.setdefault(who, 0)
            self.log(f"player {who} joined")
        elif payload == "player_removed":
            who = ev.player_removed.who
            self.players.pop(who, None)
            self.active_bets.pop(who, None)
            self.showdown.pop(who, None)
            self.log(f"player {who} left")
        elif payload == "player_chips":
            who = ev.player_chips.who
            self.players[who] = ev.player_chips.chips
        elif payload == "hand_started":
            self.reset_hand()
            self.log("hand started")
        elif payload == "phase_advanced":
            self.phase = ev.phase_advanced.next
            self.active_bets = {}
            self.log(f"phase: {PHASE_LABELS.get(self.phase, '?')}")
        elif payload == "dealt_hole":
            who = ev.dealt_hole.who
            if self.player_id is None:
                self.player_id = who
                self.log(f"you are player {who}")
            if who == self.player_id:
                self.hole = list(ev.dealt_hole.hole)
        elif payload == "dealt_flop":
            self.board = list(ev.dealt_flop.flop)
        elif payload == "dealt_street":
            self.board.append(ev.dealt_street.street)
        elif payload == "bet_placed":
            who = ev.bet_placed.who
            self.active_bets[who] = self.active_bets.get(who, 0) + ev.bet_placed.amount
            self.pot += ev.bet_placed.amount
            self.log(f"player {who} bet {ev.bet_placed.amount}")
        elif payload == "turn_advanced":
            self.turn = ev.turn_advanced.next
        elif payload == "won_pot":
            self.pot = max(0, self.pot - ev.won_pot.amount)
            self.log(f"player {ev.won_pot.who} won {ev.won_pot.amount}")
        elif payload == "showdown_hand":
            self.showdown[ev.showdown_hand.who] = list(ev.showdown_hand.hole)
            self.log(f"showdown: player {ev.showdown_hand.who}")


def card_str(card: cards_pb2.Card) -> str:
    rank = RANK_LABELS.get(card.rank, "?")
    suit = SUIT_SYMBOLS.get(card.suit, "?")
    return f"{rank}{suit}"


def render_cards(cards) -> str:
    if not cards:
        return "-"
    return " ".join(card_str(c) for c in cards)


def error_to_str(err) -> str:
    which = err.WhichOneof("payload")
    if which is None:
        return "error"
    value = getattr(err, which)
    name = err.DESCRIPTOR.fields_by_name[which].enum_type.values[value].name
    return name.lower()


def render_screen(win, state: ClientState, selection: int, bet_amount: int):
    win.erase()
    h, w = win.getmaxyx()

    def safe_addstr(y, x, text):
        if y < 0 or y >= h or x >= w:
            return
        max_len = max(0, w - x - 1)
        win.addstr(y, x, text[:max_len])

    title = "Poker Client"
    safe_addstr(0, 2, title)

    phase_label = PHASE_LABELS.get(state.phase, "?")
    safe_addstr(1, 2, f"Phase: {phase_label}  Pot: {state.pot}")
    safe_addstr(2, 2, f"Board: {render_cards(state.board)}")

    chips = state.player_chips()
    safe_addstr(3, 2, f"You: {render_cards(state.hole)}  Chips: {chips}")

    safe_addstr(5, 2, "Players:")
    row = 6
    for pid in sorted(state.players.keys()):
        tag = "->" if pid == state.turn else "  "
        line = f"{tag} P{pid}  chips={state.players.get(pid, 0)}"
        if row < h - 10:
            safe_addstr(row, 4, line)
        row += 1

    if state.showdown:
        safe_addstr(row + 1, 2, "Showdown:")
        row += 2
        for pid in sorted(state.showdown.keys()):
            cards = render_cards(state.showdown[pid])
            if row < h - 6:
                safe_addstr(row, 4, f"P{pid}: {cards}")
            row += 1

    action_row = h - 6
    safe_addstr(action_row, 2, "Actions:")

    call_amount = state.call_amount()
    call_label = f"Call {call_amount}" if call_amount > 0 else "Check"
    bet_min = max(call_amount, 1) if chips > 0 else 0
    bet_max = chips
    if bet_min > bet_max:
        bet_display = "Bet (unavailable)"
    else:
        bet_display = f"Bet {bet_amount} [{bet_min}-{bet_max}]"
    actions = [call_label, bet_display, "Fold"]

    for idx, label in enumerate(actions):
        marker = ">" if idx == selection else " "
        safe_addstr(action_row + 1 + idx, 4, f"{marker} {label}")

    if state.turn is None:
        status = "Waiting for server"
    elif state.turn == state.player_id:
        status = f"Your turn: P{state.player_id}"
    else:
        status = f"Waiting for P{state.turn}"
    safe_addstr(h - 2, 2, status)

    if state.logs:
        log_row = action_row - 2
        if log_row > row + 1:
            safe_addstr(log_row, 2, state.logs[-1])

    win.border()
    win.noutrefresh()


def main(stdscr):
    curses.curs_set(0)
    stdscr.nodelay(True)
    stdscr.keypad(True)

    height, width = stdscr.getmaxyx()
    view_h = min(24, height)
    view_w = min(100, width)
    win = curses.newwin(view_h, view_w, 0, 0)

    host = "127.0.0.1"
    port = 65432

    sel = selectors.DefaultSelector()
    out_buf = bytearray()
    in_buf = FrameBuffer()
    state = ClientState()

    selection = 0
    bet_amount = 0

    def update_interest(sock):
        events = selectors.EVENT_READ
        if out_buf:
            events |= selectors.EVENT_WRITE
        sel.modify(sock, events, data="sock")

    def queue_action(sock, action):
        payload = action.SerializeToString()
        out_buf.extend(frame_message(payload))
        update_interest(sock)
        state.log(f"sent {action_to_str(action)}")

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.setblocking(False)
        s.connect_ex((host, port))
        sel.register(s, selectors.EVENT_READ, data="sock")

        state.log(f"connected to {host}:{port}")

        while True:
            for key, mask in sel.select(timeout=0.05):
                if key.data != "sock":
                    continue
                if mask & selectors.EVENT_READ:
                    data = s.recv(4096)
                    if not data:
                        state.log("server closed")
                        raise ConnectionError("server closed")
                    for frame in in_buf.feed(data):
                        resp = response_pb2.Response()
                        resp.ParseFromString(frame)
                        for msg in resp.messages:
                            payload = msg.WhichOneof("payload")
                            if payload == "event":
                                state.apply_event(msg.event)
                            elif payload == "error":
                                state.log(f"error: {error_to_str(msg.error)}")
                if mask & selectors.EVENT_WRITE:
                    if out_buf:
                        sent = s.send(out_buf)
                        del out_buf[:sent]
                    if not out_buf:
                        update_interest(s)

            key = stdscr.getch()
            if key != -1:
                if key in (ord("q"), ord("Q")):
                    break
                if key == curses.KEY_UP:
                    selection = (selection - 1) % 3
                elif key == curses.KEY_DOWN:
                    selection = (selection + 1) % 3
                elif key in (curses.KEY_LEFT, curses.KEY_RIGHT):
                    chips = state.player_chips()
                    call_amount = state.call_amount()
                    bet_min = max(call_amount, 1) if chips > 0 else 0
                    bet_max = chips
                    if bet_min <= bet_max and selection == 1:
                        step = 1
                        if key == curses.KEY_LEFT:
                            bet_amount = max(bet_min, bet_amount - step)
                        else:
                            bet_amount = min(bet_max, bet_amount + step)
                elif key in (curses.KEY_ENTER, 10, 13):
                    if state.player_id is None or state.turn is None:
                        state.log("waiting for hand")
                    elif state.turn != state.player_id:
                        state.log("not your turn")
                    else:
                        action = actions_pb2.Action()
                        if selection == 0:
                            amount = min(state.call_amount(), state.player_chips())
                            action.bet.amount = amount
                            queue_action(s, action)
                        elif selection == 1:
                            chips = state.player_chips()
                            call_amount = state.call_amount()
                            bet_min = max(call_amount, 1) if chips > 0 else 0
                            bet_max = chips
                            if bet_min > bet_max:
                                state.log("bet unavailable")
                            else:
                                if bet_amount < bet_min:
                                    bet_amount = bet_min
                                action.bet.amount = bet_amount
                                queue_action(s, action)
                        else:
                            action.fold.SetInParent()
                            queue_action(s, action)

            chips = state.player_chips()
            call_amount = state.call_amount()
            bet_min = max(call_amount, 1) if chips > 0 else 0
            bet_max = chips
            if bet_amount == 0 and bet_min <= bet_max:
                bet_amount = bet_min
            elif bet_min > bet_max:
                bet_amount = 0
            else:
                bet_amount = min(max(bet_amount, bet_min), bet_max)

            render_screen(win, state, selection, bet_amount)
            curses.doupdate()


if __name__ == "__main__":
    try:
        curses.wrapper(main)
    except KeyboardInterrupt:
        sys.exit(0)
