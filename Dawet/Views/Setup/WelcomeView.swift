//
//  WelcomeView.swift
//  Dawet
//
//  This file is part of Dawet.
//
//  Dawet is free software: you can redistribute it and/or modify it under the terms
//  of the GNU General Public License as published by the Free Software Foundation,
//  either version 3 of the License, or (at your option) any later version.
//
//  Dawet is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
//  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
//  See the GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License along with Dawet.
//  If not, see https://www.gnu.org/licenses/.
//

import SwiftUI
import DawetKit

struct WelcomeView: View {
    @State var rosettaInstalled: Bool?
    @State var dawetWineInstalled: Bool?
    @State var shouldCheckInstallStatus: Bool = false
    @Binding var path: [SetupStage]
    @Binding var showSetup: Bool
    var firstTime: Bool

    var body: some View {
        VStack {
            VStack {
                if firstTime {
                    Text("setup.welcome")
                        .font(.title)
                        .fontWeight(.bold)
                    Text("setup.welcome.subtitle")
                        .font(.subheadline)
                        .foregroundStyle(.secondary)
                } else {
                    Text("setup.title")
                        .font(.title)
                        .fontWeight(.bold)
                    Text("setup.subtitle")
                        .font(.subheadline)
                        .foregroundStyle(.secondary)
                }
            }
            .padding(.horizontal)
            Spacer()
            Form {
                InstallStatusView(isInstalled: $rosettaInstalled,
                                  shouldCheckInstallStatus: $shouldCheckInstallStatus,
                                  name: "Rosetta")
                InstallStatusView(isInstalled: $dawetWineInstalled,
                                  shouldCheckInstallStatus: $shouldCheckInstallStatus,
                                  showUninstall: true,
                                  name: "DawetWine")
            }
            .formStyle(.grouped)
            .scrollDisabled(true)
            .onAppear {
                checkInstallStatus()
            }
            .onChange(of: shouldCheckInstallStatus) {
                checkInstallStatus()
            }
            Spacer()
            HStack {
                if let rosettaInstalled = rosettaInstalled,
                   let dawetWineInstalled = dawetWineInstalled {
                    if !rosettaInstalled || !dawetWineInstalled {
                        Button("setup.quit") {
                            exit(0)
                        }
                        .keyboardShortcut(.cancelAction)
                    }
                    Spacer()
                    Button(rosettaInstalled && dawetWineInstalled ? "setup.done" : "setup.next") {
                        if !rosettaInstalled {
                            path.append(.rosetta)
                            return
                        }

                        if !dawetWineInstalled {
                            path.append(.dawetWineDownload)
                            return
                        }

                        showSetup = false
                    }
                    .keyboardShortcut(.defaultAction)
                }
            }
        }
        .frame(width: 400, height: 200)
    }

    func checkInstallStatus() {
        rosettaInstalled = Rosetta2.isRosettaInstalled
        dawetWineInstalled = DawetWineInstaller.isDawetWineInstalled()
    }
}

struct InstallStatusView: View {
    @Binding var isInstalled: Bool?
    @Binding var shouldCheckInstallStatus: Bool
    @State var showUninstall: Bool = false
    @State var name: String
    @State var text: String = String(localized: "setup.install.checking")

    var body: some View {
        HStack {
            Group {
                if let installed = isInstalled {
                    Circle()
                        .foregroundColor(installed ? .green : .red)
                } else {
                    ProgressView()
                        .controlSize(.small)
                }
            }
            .frame(width: 10)
            Text(String.init(format: text, name))
            Spacer()
            if let installed = isInstalled {
                if installed && showUninstall {
                    Button("setup.uninstall") {
                        uninstall()
                    }
                }
            }
        }
        .onChange(of: isInstalled) {
            if let installed = isInstalled {
                if installed {
                    text = String(localized: "setup.install.installed")
                } else {
                    text = String(localized: "setup.install.notInstalled")
                }
            } else {
                text = String(localized: "setup.install.checking")
            }
        }
    }

    func uninstall() {
        if name == "DawetWine" {
            DawetWineInstaller.uninstall()
        }

        shouldCheckInstallStatus.toggle()
    }
}
