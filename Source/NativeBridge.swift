import SwiftUI
import AppKit

struct CheckItem: Identifiable {
    let id: String; let name: String; let ok: Bool; let detail: String; let installable: Bool
}
struct AccountOption: Identifiable { let id: String; let name: String }

@MainActor final class BuildModel: ObservableObject {
    @Published var folder = ""
    @Published var keys = ""
    @Published var output = FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("Downloads/NativeBridge Builds").path
    @Published var backend = "auto"
    @Published var target = "mac"
    @Published var saveImport = ""
    @Published var cheatImport = ""
    @Published var saveVersion = "4.0.1"
    @Published var firmware = ""
    @Published var base = ""
    @Published var update = ""
    @Published var install = false
    @Published var free = true
    @Published var team = ""
    @Published var device = ""
    @Published var log = "Choose a directory containing one game, its updates and DLC.\n"
    @Published var status = "Ready to build"
    @Published var running = false
    @Published var fraction = 0.0
    @Published var artifact = ""
    @Published var estimate = 2700.0
    @Published var estimateLabel = "Rough first-build estimate"
    @Published var start: Date?
    @Published var failed = false
    @Published var setupSheet = false
    @Published var scope = "selected"
    @Published var cache = FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("Library/Application Support/NativeBridge").path
    @Published var checks: [CheckItem] = []
    @Published var fileChecks: [CheckItem] = []
    @Published var teams: [AccountOption] = []
    @Published var devices: [AccountOption] = []
    @Published var setupDisk = ""
    @Published var fileDisk = ""
    @Published var missing: [String] = []
    @Published var setupGB = 0.0
    @Published var setupReady = false
    @Published var toolsReady = false
    @Published var filesReady = false
    var process: Process?
    var pending = Data()
    var streamEnded = false
    var exitStatus: Int32?
    var currentAction = ""
    func finishIfReady() {
        guard streamEnded, let code = exitStatus else { return }
        running = false
        if code != 0 && !failed { failed = true; status = "Operation stopped. Check the build log." }
        if let u = configURL { try? FileManager.default.removeItem(at:u) }; configURL = nil
        process = nil; exitStatus = nil
        if currentAction == "prebuild" && code == 0 { approveBuild() }
    }
    var configURL: URL?
    init() {
        let d = UserDefaults.standard
        for key in ["folder","keys","output","firmware","team","device","cache"] {
            guard let value = d.string(forKey: key) else { continue }
            switch key { case "folder": folder = value; case "keys": keys = value; case "output": output = value; case "firmware": firmware = value; case "team": team = value; case "cache": cache = value; default: device = value }
        }
    }
    func parseItems(_ value: Any?) -> [CheckItem] {
        (value as? [[String:Any]] ?? []).map { CheckItem(id:$0["id"] as? String ?? UUID().uuidString,name:$0["name"] as? String ?? "Check",ok:$0["ok"] as? Bool ?? false,detail:$0["detail"] as? String ?? "",installable:$0["installable"] as? Bool ?? false) }
    }
    func diskText(_ value: Any?) -> String {
        (value as? [[String:Any]] ?? []).map { d in
            let free = (d["free"] as? Double ?? 0)/pow(1024,3)
            let needed = (d["required"] as? Double ?? 0)/pow(1024,3)
            return String(format:"%@  •  %.1f GiB available / %.1f GiB estimated need (includes 10 GiB reserve)",d["path"] as? String ?? "Disk",free,needed)
        }.joined(separator:"\n")
    }
    func pythonAvailable() -> Bool {
        let p = Process(); let pipe = Pipe(); p.executableURL = URL(fileURLWithPath:"/usr/bin/xcode-select"); p.arguments = ["-p"]; p.standardOutput = pipe; p.standardError = FileHandle.nullDevice
        do { try p.run(); let data = pipe.fileHandleForReading.readDataToEndOfFile(); p.waitUntilExit(); let path = String(decoding:data,as:UTF8.self).trimmingCharacters(in:.whitespacesAndNewlines)
            return p.terminationStatus == 0 && FileManager.default.isExecutableFile(atPath:path+"/usr/bin/python3")
        } catch { return false }
    }
    func checkSetup() {
        guard !running else { return }
        if pythonAvailable() { run("check"); return }
        toolsReady = false; setupReady = false
        checks = [CheckItem(id:"xcode",name:"Xcode development tools",ok:false,detail:"Install Xcode, open it to finish setup, and select it under Settings → Locations → Command Line Tools. Then check again.",installable:false)]
        let attrs = try? FileManager.default.attributesOfFileSystem(forPath:NSHomeDirectory())
        let bytes = (attrs?[.systemFreeSize] as? NSNumber)?.doubleValue ?? 0
        setupDisk = String(format:"System volume: %.1f GiB free. Allow roughly 40 GiB for Xcode installation plus 10 GiB reserve. Apple displays the actual download size.",bytes/pow(1024,3))
        status = "Xcode setup is required before detailed checks. Nothing downloaded."
    }
    func confirm(_ title: String, _ text: String, button: String) -> Bool {
        let alert = NSAlert(); alert.messageText = title; alert.informativeText = text; alert.addButton(withTitle:button); alert.addButton(withTitle:"Cancel")
        return alert.runModal() == .alertFirstButtonReturn
    }
    func approveTools() {
        guard setupReady, !running else { return }
        let names = checks.filter { !$0.ok && $0.installable }.map(\.name).joined(separator:", ")
        if names.isEmpty { status = "All tools in this setup scope are already installed."; return }
        if confirm("Download and install missing tools?", "Install: \(names).\n\nThis includes their required dependencies. Downloads come from Apple, Homebrew and the tools’ official distribution sources. Estimated temporary + installed footprint: \(Int(setupGB)) GiB; exact download sizes vary.\n\n\(setupDisk)\n\nExisting tools are reused. An administrator prompt may require you to finish installation.",button:"Download & install") { run("setup",approved:missing) }
    }
    func approveBuild() {
        guard filesReady && toolsReady else { status = "Resolve the checklist issues before building."; setupSheet = !toolsReady; return }
        if confirm("Allow this build’s downloads?", "The selected backend will be downloaded if missing. Its build scripts may fetch Python packages, Rust crates, source dependencies, shader tooling and signing assets. Your game packages and keys are used locally, with game data copied to your iPhone only if installation is enabled.\n\n\(fileDisk)\n\nSpace figures are estimates. The build stops below 5 GiB free. Apple provisioning may require interaction in Xcode.",button:"Allow & build") { run("build",allowDownloads:true) }
    }
    func openXcode() {
        if let url = NSWorkspace.shared.urlForApplication(withBundleIdentifier:"com.apple.dt.Xcode") {
            NSWorkspace.shared.open(url)
            let a = NSAlert(); a.messageText = "Sign in securely in Xcode"; a.informativeText = "Open Xcode → Settings → Accounts, click +, and sign in with your Apple Account. A free Personal Team can be used. Then return here and click Refresh accounts & devices.\n\nIf your team is not detected, use Manage Certificates in Xcode to create an Apple Development certificate. NativeBridge never asks for or stores your Apple password."; a.addButton(withTitle:"OK"); a.runModal()
        } else { getXcode() }
    }
    func getXcode() {
        if confirm("Open Apple’s Xcode download page?", "Xcode is free. Its download and installation can need roughly 40 GiB plus a 10 GiB reserve. You will approve installation and Apple’s license in the App Store/Xcode. Nothing is downloaded by this button.",button:"Open App Store") { NSWorkspace.shared.open(URL(string:"https://apps.apple.com/app/xcode/id497799835")!) }
    }
    func installHomebrew() {
        guard !running else { return }
        let attrs = try? FileManager.default.attributesOfFileSystem(forPath:NSHomeDirectory())
        let free = (attrs?[.systemFreeSize] as? NSNumber)?.doubleValue ?? 0
        guard free > 12*pow(1024,3) else { status = "Homebrew setup needs at least 12 GiB free including reserve."; return }
        guard confirm("Download Homebrew’s installer?", "Download the official installer from github.com/Homebrew/install and run it in Terminal. Allow about 2 GiB plus 10 GiB reserve. The installer will show its changes and may ask for your Mac administrator password. No password is entered into NativeBridge. Return here and check again when it finishes.",button:"Download & open installer") else { return }
        do {
            let dir = FileManager.default.temporaryDirectory.appendingPathComponent("NativeBridge-Homebrew-\(UUID().uuidString)")
            try FileManager.default.createDirectory(at:dir,withIntermediateDirectories:true,attributes:[.posixPermissions:0o700])
            let installer = dir.appendingPathComponent("install.sh")
            let command = dir.appendingPathComponent("Install Homebrew.command")
            let p = Process(); p.executableURL = URL(fileURLWithPath:"/usr/bin/curl"); p.arguments = ["--fail","--location","--proto","=https","--tlsv1.2","--max-time","120","https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh","-o",installer.path]
            p.standardOutput = FileHandle.nullDevice; p.standardError = FileHandle.nullDevice
            running = true; status = "Downloading the approved Homebrew installer…"; process = p
            p.terminationHandler = { proc in Task { @MainActor in
                self.running = false; self.process = nil
                guard proc.terminationStatus == 0 else { self.status = "Homebrew installer download failed or was cancelled. Nothing installed."; return }
                do {
                    let quoted = "'"+installer.path.replacingOccurrences(of:"'",with:"'\\''")+"'"
                    let script = "#!/bin/bash\n/bin/bash \(quoted)\nresult=$?\nprintf '\\nInstaller finished (exit %s). Return to NativeBridge and Check again.\\n' \"$result\"\nread -r -p 'Press Return to close.'\nexit \"$result\"\n"
                    try script.write(to:command,atomically:true,encoding:.utf8)
                    try FileManager.default.setAttributes([.posixPermissions:0o700],ofItemAtPath:command.path)
                    NSWorkspace.shared.open(command)
                    self.status = "Finish Homebrew installation in Terminal, then Check again."
                } catch { self.status = error.localizedDescription }
            } }
            try p.run()
        } catch { running = false; status = error.localizedDescription }
    }

    func choose(_ label: String, directory: Bool, completion: (String)->Void) {
        let p = NSOpenPanel(); p.title = label; p.canChooseFiles = !directory; p.canChooseDirectories = directory; p.allowsMultipleSelection = false
        if p.runModal() == .OK, let url = p.url { completion(url.path) }
    }
    func receive(_ data: Data) {
        pending.append(data)
        while let range = pending.range(of: Data([10])) {
            let line = pending.subdata(in: 0..<range.lowerBound)
            pending.removeSubrange(0..<range.upperBound)
            guard let e = try? JSONSerialization.jsonObject(with: line) as? [String:Any], let type = e["type"] as? String else { continue }
            let text = e["text"] as? String ?? ""
            switch type {
            case "report":
                checks = parseItems(e["items"]); missing = e["missing"] as? [String] ?? []; setupDisk = diskText(e["disks"]); setupGB = e["estimatedGB"] as? Double ?? 0
                setupReady = e["setupReady"] as? Bool ?? false; toolsReady = e["ready"] as? Bool ?? false
            case "files":
                fileChecks = parseItems(e["items"]); fileDisk = diskText(e["disks"]); filesReady = e["ready"] as? Bool ?? false
            case "accounts":
                let t = e["teams"] as? [[String:Any]] ?? []
                teams = t.map { AccountOption(id:$0["id"] as? String ?? "",name:($0["name"] as? String ?? "Team")+" · "+($0["id"] as? String ?? "")) }
                devices = (e["devices"] as? [[String:String]] ?? []).map { AccountOption(id:$0["id"] ?? "",name:$0["name"] ?? "iPhone") }
                if !teams.contains(where: { $0.id == team }) { team = teams.count == 1 ? teams[0].id : "" }
                if !devices.contains(where: { $0.id == device }) { device = devices.count == 1 ? devices[0].id : "" }
                if let detected = t.first(where: { $0["id"] as? String == team }), detected["free"] as? Bool == true { free = true }
            case "stage": status = text; fraction = e["fraction"] as? Double ?? fraction
            case "estimate": estimate = e["seconds"] as? Double ?? 2700; estimateLabel = text
            case "selected": append("Selected backend: \(text)")
            case "artifact": artifact = e["path"] as? String ?? ""
            case "error": failed = true; status = text; append("ERROR: \(text)")
            case "done": status = text; append(text)
            default: append(text)
            }
        }
    }
    func append(_ text: String) { log += text + "\n"; if log.count > 180000 { log = String(log.suffix(140000)) } }
    func run(_ action: String, approved: [String] = [], allowDownloads: Bool = false) {
        guard !running else { return }
        guard pythonAvailable() else { setupSheet = true; checkSetup(); return }
        guard let script = Bundle.main.url(forResource: "engine", withExtension: "py") else { status = "Missing bundled build engine"; failed = true; return }
        let settings: [String:Any] = ["saveImport":saveImport,"cheatImport":cheatImport,"saveVersion":saveVersion,"action":action,"folder":folder,"keys":keys,"output":output,"backend":backend,"target":target,"firmware":firmware,"base":base,"update":update,"install":install && target == "ios","team":team,"device":device,"free":free,"cache":cache,"scope":scope,"approved":approved,"downloadsApproved":allowDownloads]
        for k in ["folder","keys","output","firmware","team","device","cache"] { UserDefaults.standard.set(settings[k],forKey:k) }
        do {
            let url = FileManager.default.temporaryDirectory.appendingPathComponent("nativebridge-\(UUID().uuidString).json")
            try JSONSerialization.data(withJSONObject: settings).write(to:url,options:.atomic)
            try FileManager.default.setAttributes([.posixPermissions:0o600],ofItemAtPath:url.path)
            configURL = url
            let p = Process(); p.executableURL = URL(fileURLWithPath:"/usr/bin/python3"); p.arguments = [script.path,url.path]
            let pipe = Pipe(); p.standardOutput = pipe; p.standardError = pipe
            streamEnded = false; exitStatus = nil; currentAction = action; pending = Data(); log = "NativeBridge • \(action)\n"; fraction = 0; if action == "build" { artifact = "" }; failed = false; start = Date(); estimate = 2700; status = "Checking inputs and tools…"; running = true
            pipe.fileHandleForReading.readabilityHandler = { handle in
                let data = handle.availableData
                if data.isEmpty { handle.readabilityHandler = nil; Task { @MainActor in self.streamEnded = true; self.finishIfReady() }; return }
                Task { @MainActor in self.receive(data) }
            }
            p.terminationHandler = { proc in
                Task { @MainActor in
                    self.exitStatus = proc.terminationStatus
                    self.finishIfReady()
                }
            }
            process = p; try p.run()
        } catch { running = false; failed = true; status = error.localizedDescription; if let u = configURL { try? FileManager.default.removeItem(at:u) } }
    }
    func cleanBuild() {
        if confirm("Delete this game's completed build cache?", "Removes temporary build files for the selected game/backend from the build cache. Exported apps, installed games, original game packages, keys and save files are kept. The next build will need to recreate its cache.", button:"Delete cache") { run("cleanup") }
    }
    func exportLog() {
        let panel = NSSavePanel(); panel.nameFieldStringValue = "NativeBridge-build.log"
        if panel.runModal() == .OK, let url = panel.url { try? log.write(to:url,atomically:true,encoding:.utf8) }
    }
}

struct ContentView: View {
    @StateObject var m = BuildModel()
    @State var advanced = false
    let cyan = Color(red:0.25,green:0.86,blue:0.77)
    func pathRow(_ title: String, value: Binding<String>, directory: Bool, hint: String) -> some View {
        VStack(alignment:.leading,spacing:7) {
            Text(title).font(.system(size:13,weight:.semibold))
            HStack {
                Image(systemName:directory ? "folder" : "key.horizontal").foregroundStyle(cyan)
                TextField(hint,text:value).textFieldStyle(.plain).font(.system(size:12)).lineLimit(1)
                Button("Choose…") { m.choose(title,directory:directory) { value.wrappedValue = $0 } }.buttonStyle(.bordered)
            }.padding(10).background(Color.white.opacity(0.055),in:RoundedRectangle(cornerRadius:9))
        }
    }
    var setupView: some View {
        VStack(alignment:.leading,spacing:18) {
            Text("Let’s get your Mac ready.").font(.system(size:25,weight:.bold))
            Text("We check first. You approve downloads. Installed tools are kept.").foregroundStyle(.secondary)
            Text("Checking only the selected game / backend and target platform. Choose your game folder first when using Automatic.").font(.callout).foregroundStyle(.secondary)
            ScrollView {
                VStack(alignment:.leading,spacing:13) {
                    ForEach(m.checks) { item in
                        HStack(alignment:.top) {
                            Image(systemName:item.ok ? "checkmark.circle.fill" : "circle.dashed").foregroundStyle(item.ok ? cyan : .orange)
                            VStack(alignment:.leading,spacing:3) {
                                Text(item.name + (item.ok ? " · Installed / ready" : " · Needs attention")).font(.system(size:13,weight:.medium))
                                Text(item.detail).font(.system(size:11)).foregroundStyle(.secondary).textSelection(.enabled)
                            }
                        }
                    }
                    Text(m.setupDisk).font(.system(size:11,design:.monospaced)).foregroundStyle(.secondary).textSelection(.enabled)
                    Text("Estimates include temporary installation files, dependencies and a 10 GiB reserve. Actual sizes vary. Game builds need additional space checked separately.").font(.system(size:11)).foregroundStyle(.secondary)
                }.frame(maxWidth:.infinity,alignment:.leading)
            }.frame(minHeight:250,maxHeight:380)
            if m.running { ProgressView(); Text(m.status).font(.system(size:12)) }
            if !m.running { Text(m.status).font(.system(size:12)).foregroundStyle(m.failed ? .orange : .secondary) }
            HStack {
                if m.checks.contains(where: { $0.id == "xcode" && !$0.ok }) { Button("Get Xcode…") { m.getXcode() } }
                if m.checks.contains(where: { $0.id == "homebrew" && !$0.ok }) { Button("Install Homebrew…") { m.installHomebrew() } }
                Button("Open Xcode / Apple sign-in…") { m.openXcode() }
            }.disabled(m.running)
            HStack {
                Button("Check again") { m.checkSetup() }.disabled(m.running)
                Button("Install missing tools…") { m.approveTools() }.buttonStyle(.borderedProminent).tint(cyan).foregroundStyle(.black).disabled(m.running || !m.setupReady || m.missing.isEmpty)
                Spacer()
                if m.running { Button("Cancel operation") { m.process?.terminate() } }
                Button("Continue") { UserDefaults.standard.set(true,forKey:"setupV2Seen"); m.setupSheet = false }.disabled(m.running)
            }
            Text("Apple sign-in happens in Xcode. This app does not store your password or implement AltStore’s login protocol.").font(.system(size:10)).foregroundStyle(.secondary)
        }.padding(28).frame(width:720).preferredColorScheme(.dark)
        .onChange(of:m.scope) { _, _ in m.checkSetup() }
    }
    var body: some View {
        HStack(spacing:0) {
            VStack(alignment:.leading,spacing:22) {
                Image(systemName:"shippingbox.fill").font(.system(size:34)).foregroundStyle(cyan)
                Text("NativeBridge").font(.system(size:23,weight:.bold)).lineLimit(1).minimumScaleFactor(0.85)
                Text("YOUR GAMES.\nAPPLE HARDWARE.").font(.system(size:10,weight:.bold,design:.monospaced)).tracking(1.8).foregroundStyle(.secondary)
                Divider()
                Label("Build a game",systemImage:"hammer.fill").foregroundStyle(cyan)
                Button { m.setupSheet = true; m.checkSetup() } label: { Label("Setup & checks",systemImage:"checklist") }.buttonStyle(.plain).disabled(m.running)
                Text("Local files in.\nNative apps out.").font(.system(size:14)).foregroundStyle(.secondary).lineSpacing(5)
                Spacer()
                VStack(alignment:.leading,spacing:12) {
                    Text("BACKENDS").font(.system(size:10,weight:.bold)).foregroundStyle(.secondary)
                    Link("MacNativeGames ↗",destination:URL(string:"https://github.com/ly05010419/MacNativeGames")!)
                    Link("Mush8 · Mario Kart ↗",destination:URL(string:"https://github.com/c22dev/mush8")!)
                    Link("nxnative ↗",destination:URL(string:"https://github.com/seedds/nxnative")!)
                }.font(.system(size:12))
                Text("Use your own game dumps, keys and firmware. Compatibility depends on the game and backend.").font(.system(size:11)).foregroundStyle(.secondary).fixedSize(horizontal:false,vertical:true)
            }.padding(26).frame(width:220).frame(maxHeight:.infinity).background(Color(red:0.055,green:0.075,blue:0.10))
            ScrollView {
                VStack(alignment:.leading,spacing:22) {
                    HStack(alignment:.top) {
                        VStack(alignment:.leading,spacing:6) {
                            Text("From your library to your device.").font(.system(size:26,weight:.bold))
                            Text("Select one game folder. We’ll connect the build tools.").foregroundStyle(.secondary)
                        }
                        Spacer()
                        Text("LOCAL BUILD").font(.system(size:10,weight:.bold)).tracking(1).padding(8).background(cyan.opacity(0.12),in:Capsule()).foregroundStyle(cyan)
                    }
                    VStack(alignment:.leading,spacing:16) {
                        pathRow("01   Game directory",value:$m.folder,directory:true,hint:"Base game + update + DLC in one folder")
                        pathRow("02   prod.keys",value:$m.keys,directory:false,hint:"Your prod.keys file, or paste its directory")
                        pathRow("03   Save builds to",value:$m.output,directory:true,hint:"Choose an output directory")
                        HStack {
                            Picker("Build for",selection:$m.target) { Text("Mac · .app").tag("mac"); Text("iPhone / iPad · .ipa").tag("ios") }.pickerStyle(.segmented)
                            Picker("Backend",selection:$m.backend) { Text("Automatic").tag("auto"); Text("MacNativeGames · Zelda").tag("macnative"); Text("Mush8 · Mario Kart").tag("mush8"); Text("nxnative · Other games").tag("nxnative") }.frame(width:280)
                        }
                        Text("Automatic routing uses filenames and title IDs: Zelda → MacNativeGames, Mario Kart → Mush8, others → nxnative. Select nxnative for Mario Kart XCI files.").font(.system(size:11)).foregroundStyle(.secondary)
                        DisclosureGroup("Advanced · firmware and package selection",isExpanded:$advanced) {
                            VStack(spacing:13) {
                                pathRow("Firmware directory (required for Mush8 and Zelda NSP)",value:$m.firmware,directory:true,hint:"Unpacked firmware from your Switch")
                                pathRow("Build cache location",value:$m.cache,directory:true,hint:"Choose a drive with enough free space")
                                pathRow("Base game override",value:$m.base,directory:false,hint:"Optional — resolve ambiguous file names")
                                pathRow("Update override",value:$m.update,directory:false,hint:"Optional — select the update to use")
                            }.padding(.top,12)
                        }
                        if m.target == "ios" {
                            Divider()
                            pathRow("Import a save (optional)",value:$m.saveImport,directory:true,hint:"Unpacked save contents, not the title/user wrapper folder")
                            Text("Seeds an empty save folder on next launch. Zelda/nxnative: select raw save contents. Mush8: select its native save folder containing typeN-indexN folders. Existing progress is preserved. Clear the path to omit the import.").font(.system(size:11)).foregroundStyle(.secondary)
                            Picker("Mario Kart save version (Mush8 only)",selection:$m.saveVersion) { Text("4.0.1").tag("4.0.1"); Text("3.0.5").tag("3.0.5") }
                            pathRow("Archive cheat files (optional · inactive)",value:$m.cheatImport,directory:true,hint:"Title folder containing cheats/BUILD_ID.txt files")
                            Text("Stores Eden-format text with the app. These native backends do not execute Eden cheat codes; importing does not activate cheats.").font(.system(size:11)).foregroundStyle(.secondary)
                            Label("Includes an in-game settings menu",systemImage:"gearshape.fill").foregroundStyle(cyan)
                            Text("Change touch-control opacity and supported rendering options on your iPhone. Graphics changes need a restart. Existing apps must be rebuilt.").font(.system(size:11)).foregroundStyle(.secondary)
                            Toggle("Sign and install on my iPhone after building",isOn:$m.install).onChange(of:m.install) { _, enabled in if enabled && !m.running { m.run("accounts") } }
                            if m.install {
                                HStack {
                                    Button("Sign in through Xcode…") { m.openXcode() }
                                    Button("Refresh accounts & devices") { m.run("accounts") }
                                }
                                Picker("Apple signing team",selection:$m.team) {
                                    Text(m.teams.isEmpty ? "Sign into Xcode, then refresh" : "Choose your team").tag("")
                                    ForEach(m.teams) { Text($0.name).tag($0.id) }
                                }
                                Picker("Install on",selection:$m.device) {
                                    Text(m.devices.isEmpty ? "Connect and trust your iPhone, then refresh" : "Choose your iPhone").tag("")
                                    ForEach(m.devices) { Text($0.name).tag($0.id) }
                                }
                                Toggle("Free Apple account (Mush8 signing)",isOn:$m.free).font(.system(size:12))
                                Text("Connect and unlock your iPhone, enable Developer Mode, and sign into Xcode → Settings → Accounts. Apple provisioning and game memory requirements still apply. Mush8 copies your game data into the installed app.").font(.system(size:11)).foregroundStyle(.secondary)
                            } else { Text("IPA files are unsigned. Enable installation to sign with Xcode, or use your own sideloading tool.").font(.system(size:11)).foregroundStyle(.secondary) }
                        }
                    }.disabled(m.running)
                    HStack(spacing:12) {
                        Button { m.run("prebuild") } label: { Label("Build game",systemImage:"hammer.fill").padding(.horizontal,14).padding(.vertical,5) }.buttonStyle(.borderedProminent).tint(cyan).foregroundStyle(.black).disabled(m.running || m.folder.isEmpty || m.keys.isEmpty || m.output.isEmpty)
                        Button("Check files & space") { m.run("inspect") }.disabled(m.running || m.folder.isEmpty)
                        Button("Delete completed cache…") { m.cleanBuild() }.disabled(m.running || m.folder.isEmpty)
                        Button("Setup tools…") { m.setupSheet = true; m.checkSetup() }.disabled(m.running)
                        Spacer()
                        if m.running { Button("Cancel") { m.status = "Cancelling…"; m.process?.terminate() } }
                    }
                    Text("Setup checks are local. Every installation/build download requires approval. Existing tools are reused. Disk estimates include temporary files and a reserve.").font(.system(size:11)).foregroundStyle(.secondary)
                    if !m.fileChecks.isEmpty {
                        VStack(alignment:.leading,spacing:9) {
                            Text("FILES & DISK SPACE").font(.system(size:11,weight:.bold))
                            ForEach(m.fileChecks) { item in
                                HStack(alignment:.top) {
                                    Image(systemName:item.ok ? "checkmark.circle.fill" : "exclamationmark.circle.fill").foregroundStyle(item.ok ? cyan : .orange)
                                    VStack(alignment:.leading) { Text(item.name).font(.system(size:12,weight:.medium)); Text(item.detail).font(.system(size:11)).foregroundStyle(.secondary) }
                                }
                            }
                            Text(m.fileDisk).font(.system(size:11,design:.monospaced)).foregroundStyle(.secondary)
                        }.padding(14).background(Color.white.opacity(0.04),in:RoundedRectangle(cornerRadius:10))
                    }
                    VStack(alignment:.leading,spacing:12) {
                        HStack {
                            Image(systemName:m.failed ? "exclamationmark.circle.fill" : (m.running ? "gearshape.2.fill" : "circle.fill")).foregroundStyle(m.failed ? .orange : cyan)
                            Text(m.status).font(.system(size:13,weight:.medium)).textSelection(.enabled)
                            Spacer()
                        }
                        if m.running {
                            ProgressView().progressViewStyle(.linear)
                            TimelineView(.periodic(from:.now,by:1)) { context in
                                let elapsed = context.date.timeIntervalSince(m.start ?? context.date)
                                let remaining = max(0,m.estimate-elapsed)
                                HStack {
                                    Text("Elapsed \(Int(elapsed)/60)m \(Int(elapsed)%60)s")
                                    Spacer()
                                    Text(remaining > 0 ? "Approx. \(Int(remaining/60)+1)m left · around \((m.start ?? context.date).addingTimeInterval(m.estimate).formatted(date:.omitted,time:.shortened))" : "Taking longer than estimated — still working")
                                }.font(.system(size:11,design:.monospaced)).foregroundStyle(.secondary)
                            }
                            Text(m.estimateLabel+". This is an estimate, not measured progress.").font(.system(size:10)).foregroundStyle(.secondary)
                        }
                        HStack {
                            Text("BUILD LOG").font(.system(size:10,weight:.bold)).tracking(1).foregroundStyle(.secondary)
                            Spacer()
                            Button("Save log…") { m.exportLog() }.font(.system(size:11))
                            if !m.artifact.isEmpty { Button("Show build in Finder") { NSWorkspace.shared.activateFileViewerSelecting([URL(fileURLWithPath:m.artifact)]) } }
                        }
                        ScrollView([.vertical,.horizontal]) { Text(m.log).font(.system(size:11,design:.monospaced)).textSelection(.enabled).frame(maxWidth:.infinity,alignment:.leading).padding(12) }.frame(height:180).background(.black.opacity(0.28),in:RoundedRectangle(cornerRadius:8))
                    }.padding(16).background(Color.white.opacity(0.035),in:RoundedRectangle(cornerRadius:12))
                }.padding(30)
            }
        }.frame(minWidth:1050,minHeight:790).preferredColorScheme(.dark)
        .task {
            if !UserDefaults.standard.bool(forKey:"setupV2Seen") { m.setupSheet = true }
            m.checkSetup()
        }
        .sheet(isPresented:$m.setupSheet) { setupView }
        .onReceive(NotificationCenter.default.publisher(for:NSApplication.willTerminateNotification)) { _ in m.process?.terminate() }
    }
}

@main struct NativeBridgeApp: App {
    var body: some Scene { WindowGroup { ContentView() }.windowStyle(.titleBar).defaultSize(width:1140,height:920) }
}
