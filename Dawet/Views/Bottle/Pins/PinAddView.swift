//
//  PinAddView.swift
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

struct PinAddView: View {
    let bottle: Bottle
    @State private var showingSheet = false

    var body: some View {
        VStack {
            Button {
                showingSheet = true
            } label: {
                Image(systemName: "plus.circle")
                    .resizable()
                    .foregroundStyle(.secondary)
            }
            .buttonStyle(.plain)
            .frame(width: 45, height: 45)
            Spacer()
            Text("pin.help")
                .multilineTextAlignment(.center)
                .lineLimit(2, reservesSpace: true)
        }
        .frame(width: 90, height: 90)
        .padding(10)
        .sheet(isPresented: $showingSheet) {
            PinCreationView(bottle: bottle)
        }
    }
}

#Preview {
    PinAddView(bottle: Bottle(bottleUrl: URL(filePath: "")))
}
