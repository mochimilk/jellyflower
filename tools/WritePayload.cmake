# SPDX-License-Identifier: GPL-3.0-or-later
file(WRITE "${OUTPUT}" "201 RCDATA \"${APP}\"\n202 RCDATA \"${BRIDGE}\"\n209 RCDATA \"${START}\"\n")
foreach(pair "203|LICENSE.md" "204|THIRD_PARTY_NOTICES.md" "205|assets/LICENSE-LUCIDE.txt" "206|README.md" "207|assets/app-dark.ico" "208|assets/app-light.ico")
  string(REPLACE "|" ";" parts "${pair}")
  list(GET parts 0 id)
  list(GET parts 1 path)
  file(APPEND "${OUTPUT}" "${id} RCDATA \"${ROOT}/${path}\"\n")
endforeach()
