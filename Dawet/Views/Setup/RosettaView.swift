//
//  RosettaView.swift
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

struct RosettaView: View {
    @State var installing: Bool = true
    @State var successful: Bool = true
    @Binding var path: [SetupStage]
    @Binding var showSetup: Bool

    var body: some View {
        VStack {
            Text("setup.rosetta")
                .font(.title)
                .fontWeight(.bold)
            Text("setup.rosetta.subtitle")
                .font(.subheadline)
                .foregroundStyle(.secondary)
            Spacer()
            Group {
                if installing {
                    ProgressView()
                        .scaleEffect(2)
                } else {
                    if successful {
                        Image(systemName: "checkmark.circle")
                            .resizable()
                            .foregroundStyle(.green)
                            .frame(width: 80, height: 80)
                    } else {
                        VStack {
                            Image(systemName: "xmark.circle")
                                .resizable()
                                .foregroundStyle(.red)
                                .frame(width: 80, height: 80)
                                .padding(.bottom, 20)
                            Text("setup.rosetta.fail")
                                .font(.subheadline)
                        }
                    }
                }
            }
            Spacer()
            HStack {
                if !successful {
                    Button("setup.quit") {
                        exit(0)
                    }
                    .keyboardShortcut(.cancelAction)
                    Spacer()
                    Button("setup.retry") {
                        installing = true
                        successful = true

                        Task.detached {
                            await checkOrInstall()
                        }
                    }
                    .keyboardShortcut(.defaultAction)
                }
            }
        }
        .frame(width: 400, height: 200)
        .onAppear {
            Task.detached {
                await checkOrInstall()
            }
        }
    }

    func checkOrInstall() async {
        if Rosetta2.isRosettaInstalled {
            installing = false
            sleep(2)
            proceed()
        } else {
            do {
                successful = try await Rosetta2.installRosetta()
                installing = false
                try await Task.sleep(for: .seconds(2))
                proceed()
            } catch {
                successful = false
                installing = false
            }
        }
    }

    @MainActor
    func proceed() {
        if !DawetWineInstaller.isDawetWineInstalled() {
            path.append(.dawetWineDownload)
            return
        }

        showSetup = false
    }
}

#Preview {
    RosettaView(path: .constant([]), showSetup: .constant(true))
}
