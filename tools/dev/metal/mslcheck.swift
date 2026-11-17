import Metal
import Foundation
let dev = MTLCreateSystemDefaultDevice()!
var failed = false
for path in CommandLine.arguments.dropFirst() {
    let src = try! String(contentsOfFile: path, encoding: .utf8)
    let opts = MTLCompileOptions()
    opts.languageVersion = .version3_2
    opts.mathMode = .relaxed
    do {
        let lib = try dev.makeLibrary(source: src, options: opts)
        print("OK   \(path.split(separator: "/").last!) functions=\(lib.functionNames)")
    } catch {
        failed = true
        print("FAIL \(path.split(separator: "/").last!)\n\(error)")
    }
}
exit(failed ? 1 : 0)
