// script.js — talks to the C++ backend's /api/convert endpoint.

const API = "/api/convert";

const fields = {
  2: document.getElementById("binField"),
  8: document.getElementById("octField"),
  10: document.getElementById("decField"),
  16: document.getElementById("hexField"),
};
const registerNote = document.getElementById("registerNote");
const statusEl = document.getElementById("serverStatus");

let debounceTimer = null;

// ---------------- Backend status check ----------------

async function checkServer() {
  try {
    const res = await fetch(`${API}?value=0&from=10`, { cache: "no-store" });
    if (!res.ok) throw new Error("bad status");
    statusEl.dataset.state = "ok";
    statusEl.querySelector(".status-text").textContent = "C++ backend";
  } catch (err) {
    statusEl.dataset.state = "error";
    statusEl.querySelector(".status-text").textContent =
      "can't reach server — run ./server";
  }
}
checkServer();

// ---------------- Live quad-base register ----------------

function setRegisterError(message) {
  registerNote.textContent = message || "\u00A0";
}

function clearInvalid() {
  Object.values(fields).forEach((f) => f.closest(".cell").classList.remove("invalid"));
}

async function updateFromField(sourceBase) {
  const sourceField = fields[sourceBase];
  const value = sourceField.value.trim();

  clearInvalid();

  if (value === "") {
    setRegisterError("");
    return;
  }

  try {
    const res = await fetch(
      `${API}?value=${encodeURIComponent(value)}&from=${sourceBase}`,
      { cache: "no-store" }
    );
    const data = await res.json();

    if (!data.ok) {
      sourceField.closest(".cell").classList.add("invalid");
      setRegisterError(data.error);
      return;
    }

    setRegisterError("");
    statusEl.dataset.state = "ok";
    statusEl.querySelector(".status-text").textContent = "C++ backend";

    for (const base of Object.keys(fields)) {
      const b = Number(base);
      if (b === sourceBase) continue;
      fields[b].value = data.outputs[b] ?? "";
    }
  } catch (err) {
    statusEl.dataset.state = "error";
    statusEl.querySelector(".status-text").textContent =
      "can't reach server — run ./server";
    setRegisterError("Couldn't reach the backend. Is ./server running?");
  }
}

Object.entries(fields).forEach(([base, input]) => {
  input.addEventListener("input", () => {
    clearTimeout(debounceTimer);
    debounceTimer = setTimeout(() => updateFromField(Number(base)), 220);
  });
});

// Prime the register with the default decimal value (255) on load.
updateFromField(10);

// ---------------- Custom base converter + place-value ledger ----------------

const customForm = document.getElementById("customForm");
const customValue = document.getElementById("customValue");
const customFrom = document.getElementById("customFrom");
const customTo = document.getElementById("customTo");
const customError = document.getElementById("customError");
const ledgerResult = document.getElementById("ledgerResult");
const ledgerResultNumber = document.getElementById("ledgerResultNumber");
const pvPowers = document.getElementById("pvPowers");
const pvDigits = document.getElementById("pvDigits");
const pvValues = document.getElementById("pvValues");
const pvSum = document.getElementById("pvSum");

const DIGIT_CHARS = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

function digitValue(ch) {
  return DIGIT_CHARS.indexOf(ch.toUpperCase());
}

function renderPlaceValueLedger(digits, base, decimalStr) {
  const negative = digits.startsWith("-");
  const cleanDigits = negative ? digits.slice(1) : digits;
  const n = cleanDigits.length;
  const baseBig = BigInt(base);

  pvPowers.innerHTML = "";
  pvDigits.innerHTML = "";
  pvValues.innerHTML = "";

  let sum = 0n;
  for (let i = 0; i < n; i++) {
    const exponent = n - 1 - i;
    const dVal = BigInt(digitValue(cleanDigits[i]));
    const placeValue = dVal * baseBig ** BigInt(exponent);
    sum += placeValue;

    const powerSpan = document.createElement("span");
    powerSpan.textContent = `${base}^${exponent}`;
    pvPowers.appendChild(powerSpan);

    const digitSpan = document.createElement("span");
    digitSpan.textContent = cleanDigits[i];
    pvDigits.appendChild(digitSpan);

    const valueSpan = document.createElement("span");
    valueSpan.textContent = placeValue.toString();
    pvValues.appendChild(valueSpan);
  }

  const sumStr = (negative ? -sum : sum).toString();
  const matches = sumStr === decimalStr;
  pvSum.textContent = matches
    ? `Adds up to ${sumStr} in decimal — checks out.`
    : `Sum: ${sumStr} (decimal reported by server: ${decimalStr})`;
}

customForm.addEventListener("submit", async (e) => {
  e.preventDefault();
  customError.textContent = "";
  ledgerResult.hidden = true;

  const value = customValue.value.trim();
  const fromBase = Number(customFrom.value);
  const toBase = Number(customTo.value);

  if (!value) {
    customError.textContent = "Enter a number to convert.";
    return;
  }
  if (fromBase < 2 || fromBase > 36 || toBase < 2 || toBase > 36) {
    customError.textContent = "Bases must be between 2 and 36.";
    return;
  }

  try {
    const res = await fetch(
      `${API}?value=${encodeURIComponent(value)}&from=${fromBase}&to=${toBase}`,
      { cache: "no-store" }
    );
    const data = await res.json();

    if (!data.ok) {
      customError.textContent = data.error;
      return;
    }

    const resultDigits = data.outputs[String(toBase)];
    ledgerResultNumber.textContent = resultDigits;
    renderPlaceValueLedger(resultDigits, toBase, data.decimal);
    ledgerResult.hidden = false;
  } catch (err) {
    customError.textContent = "Couldn't reach the backend. Is ./server running?";
  }
});

// Run once on load so the ledger isn't empty.
customForm.requestSubmit ? customForm.requestSubmit() : customForm.dispatchEvent(new Event("submit"));
