// Copyright 2026 OpenArc contributors. SPDX-License-Identifier: BSD-3-Clause
const params = new URL(location.href).searchParams;
document.title = `[OpenArc probe ${params.get("run")}] ${params.get("label")}`;
