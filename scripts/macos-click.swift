// Posts a real HID mouse click at a screen coordinate, after bringing the
// target application to the front. Used by scripts/safari-online-e2e.mjs:
// Safari's WebDriver pointer actions do not generate mousemove events, so the
// page's pointer position is set from JavaScript and only the click itself
// comes from the OS.
//
//   macos-click <pid> <x> <y> [hold-ms] [move|click]
import AppKit
import CoreGraphics
import Foundation

let args = CommandLine.arguments
guard args.count >= 4, let pid = Int32(args[1]), let x = Double(args[2]),
      let y = Double(args[3]) else { exit(2) }
let hold = args.count > 4 ? Int(args[4]) ?? 150 : 150
let mode = args.count > 5 ? args[5] : "click"
if let app = NSRunningApplication(processIdentifier: pid) {
    app.activate(options: [.activateAllWindows])
}
Thread.sleep(forTimeInterval: 0.35)
let point = CGPoint(x: x, y: y)
let source = CGEventSource(stateID: .hidSystemState)
func post(_ type: CGEventType) {
    guard let event = CGEvent(mouseEventSource: source, mouseType: type,
                              mouseCursorPosition: point,
                              mouseButton: .left) else { return }
    event.setIntegerValueField(.mouseEventClickState, value: 1)
    event.post(tap: .cghidEventTap)
}
CGWarpMouseCursorPosition(point)
post(.mouseMoved)
usleep(120000)
if mode != "move" {
    post(.leftMouseDown)
    usleep(useconds_t(hold * 1000))
    post(.leftMouseUp)
    usleep(120000)
}
