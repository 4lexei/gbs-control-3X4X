"use strict";
const Structs = {
    slots: [
        { name: "name", type: "string", size: 25 },
        { name: "presetID", type: "byte", size: 1 },
        { name: "scanlines", type: "byte", size: 1 },
        { name: "scanlinesStrength", type: "byte", size: 1 },
        { name: "slot", type: "byte", size: 1 },
        { name: "wantVdsLineFilter", type: "byte", size: 1 },
        { name: "wantStepResponse", type: "byte", size: 1 },
        { name: "wantPeaking", type: "byte", size: 1 },
    ],
};
const StructParser = {
    pos: 0,
    parseStructArray(buff, structsDescriptors, struct) {
        const currentStruct = structsDescriptors[struct];
        this.pos = 0;
        const u8 = buff instanceof Uint8Array ? buff : new Uint8Array(buff);
        if (currentStruct) {
            const structSize = StructParser.getSize(structsDescriptors, struct);
            return [...Array(u8.byteLength / structSize)].map(() => {
                return currentStruct.reduce((acc, structItem) => {
                    acc[structItem.name] = this.getValue(u8, structItem);
                    return acc;
                }, {});
            });
        }
        return null;
    },
    getValue(buff, structItem) {
        switch (structItem.type) {
            case "byte":
                return buff[this.pos++];
            case "string":
                const currentPos = this.pos;
                this.pos += structItem.size;
                return [...Array(structItem.size)]
                    .map(() => " ")
                    .map((_char, index) => {
                    if (buff[currentPos + index] > 31) {
                        return String.fromCharCode(buff[currentPos + index]);
                    }
                    return "";
                })
                    .join("")
                    .trim();
        }
    },
    getSize(structsDescriptors, struct) {
        const currentStruct = structsDescriptors[struct];
        return currentStruct.reduce((acc, prop) => {
            acc += prop.size;
            return acc;
        }, 0);
    },
};
/* GBSControl Global Object*/
const GBSControl = {
    buttonMapping: {
        1: "button1280x960",
        2: "button1280x1024",
        3: "button1280x720",
        4: "button720x480",
        5: "button1920x1080",
        6: "button15kHzScaleDown",
        8: "buttonSourcePassThrough",
        9: "buttonLoadCustomPreset",
    },
    controlKeysMobileMode: "move",
    controlKeysMobile: {
        move: {
            type: "loadDoc",
            left: "7",
            up: "*",
            right: "6",
            down: "/",
        },
        scale: {
            type: "loadDoc",
            left: "h",
            up: "4",
            right: "z",
            down: "5",
        },
        borders: {
            type: "loadUser",
            left: "B",
            up: "C",
            right: "A",
            down: "D",
        },
    },
    dataQueued: 0,
    isWsActive: false,
    maxSlots: 72,
    queuedText: "",
    scanSSIDDone: false,
    scanSSIDRetries: 0,
    serverIP: "",
    structs: null,
    timeOutWs: 0,
    ui: {
        backupButton: null,
        backupInput: null,
        customSlotFilters: null,
        developerSwitch: null,
        loader: null,
        outputClear: null,
        presetButtonList: null,
        progressBackup: null,
        progressRestore: null,
        slotButtonList: null,
        slotContainer: null,
        terminal: null,
        toggleList: null,
        toggleSwichList: null,
        webSocketConnectionWarning: null,
        wifiConnect: null,
        wifiConnectButton: null,
        wifiList: null,
        wifiListTable: null,
        wifiPasswordInput: null,
        wifiSSDInput: null,
        wifiApButton: null,
        wifiStaButton: null,
        wifiStaSSID: null,
        alert: null,
        alertOk: null,
        alertContent: null,
        confirmBox: null,
        confirmOk: null,
        confirmCancel: null,
        confirmContent: null,
        prompt: null,
        promptOk: null,
        promptCancel: null,
        promptContent: null,
        promptInput: null,
    },
    updateTerminalTimer: 0,
    webSocketServerUrl: "",
    wifi: {
        mode: "ap",
        ssid: "",
    },
    ws: null,
    wsCheckTimer: 0,
    wsConnectCounter: 0,
    wsNoSuccessConnectingCounter: 0,
    wsTimeout: 0,
};
/** websocket services */
const checkWebSocketServer = () => {
    if (!GBSControl.isWsActive) {
        if (GBSControl.ws) {
            /*
                              0     CONNECTING
                              1     OPEN
                              2     CLOSING
                              3     CLOSED
                              */
            switch (GBSControl.ws.readyState) {
                case 1:
                case 2:
                    GBSControl.ws.close();
                    break;
                case 3:
                    GBSControl.ws = null;
                    break;
            }
        }
        if (!GBSControl.ws) {
            createWebSocket();
        }
    }
};
const timeOutWs = () => {
    console.log("timeOutWs");
    if (GBSControl.ws) {
        GBSControl.ws.close();
    }
    GBSControl.isWsActive = false;
    displayWifiWarning(true);
};
const updateFtlMethodRowsVisibility = (ftlOn) => {
    document.querySelectorAll("[gbs-ftl-method-rows]").forEach((row) => {
        if (ftlOn) {
            row.removeAttribute("hidden");
        }
        else {
            row.setAttribute("hidden", "");
        }
    });
};
const createWebSocket = () => {
    if (GBSControl.ws && checkReadyState()) {
        return;
    }
    GBSControl.wsNoSuccessConnectingCounter = 0;
    GBSControl.ws = new WebSocket(GBSControl.webSocketServerUrl, ["arduino"]);
    GBSControl.ws.onopen = () => {
        console.log("ws onopen");
        displayWifiWarning(false);
        GBSControl.wsConnectCounter++;
        clearTimeout(GBSControl.wsTimeout);
        GBSControl.wsTimeout = setTimeout(timeOutWs, 6000);
        GBSControl.isWsActive = true;
        GBSControl.wsNoSuccessConnectingCounter = 0;
    };
    GBSControl.ws.onclose = () => {
        console.log("ws.onclose");
        clearTimeout(GBSControl.wsTimeout);
        GBSControl.isWsActive = false;
    };
    GBSControl.ws.onmessage = (message) => {
        clearTimeout(GBSControl.wsTimeout);
        GBSControl.wsTimeout = setTimeout(timeOutWs, 2700);
        GBSControl.isWsActive = true;
        const [messageDataAt0, messageDataAt1, messageDataAt2, messageDataAt3, messageDataAt4, messageDataAt5, messageDataAt6,] = message.data;
        if (messageDataAt0 === "\x1e") {
            const bar = document.getElementById("gbsStatusBar");
            if (bar) {
                const raw = message.data.substring(1);
                markActiveModes(raw); // v107e: the old buttons read the English text, before it is translated for the screen
                updateWorkScreen(raw);
            }
            return;
        }
        if (messageDataAt0 != "#") {
            GBSControl.queuedText += message.data;
            GBSControl.dataQueued += message.data.length;
            if (GBSControl.dataQueued >= 70000) {
                GBSControl.ui.terminal.value = "";
                GBSControl.dataQueued = 0;
            }
        }
        else {
            const presetId = GBSControl.buttonMapping[messageDataAt1];
            const presetEl = document.querySelector(`[gbs-element-ref="${presetId}"]`);
            let activePresetButton = presetEl
                ? presetEl.getAttribute("gbs-element-ref")
                : "none";
            // (after v107v) with one of our modes on, Rama's resolution under it stays unlit
            if (ourModeOn && activePresetButton !== "buttonLoadCustomPreset")
                activePresetButton = "none";
            GBSControl.ui.presetButtonList.forEach(toggleButtonActive(activePresetButton));
            const slotId = "slot-" + messageDataAt2;
            const activeSlotButton = document.querySelector(`[gbs-element-ref="${slotId}"]`);
            if (activeSlotButton) {
                GBSControl.ui.slotButtonList.forEach(toggleButtonActive(slotId));
            }
            if (messageDataAt3 && messageDataAt4 && messageDataAt5) {
                const optionByte0 = messageDataAt3.charCodeAt(0);
                const optionByte1 = messageDataAt4.charCodeAt(0);
                const optionByte2 = messageDataAt5.charCodeAt(0);
                // [patch output-rate] 7th byte only exists with matching firmware; '@' (0x40) = nothing set
                const optionByte3 = messageDataAt6 ? messageDataAt6.charCodeAt(0) : 0x40;
                const optionButtonList = [
                    ...nodelistToArray(GBSControl.ui.toggleList),
                    ...nodelistToArray(GBSControl.ui.toggleSwichList),
                ];
                const toggleMethod = (button, mode) => {
                    if (button.tagName === "TD") {
                        button.innerText = mode ? "toggle_on" : "toggle_off";
                    }
                    button = button.tagName !== "TD" ? button : button.parentElement;
                    if (mode) {
                        button.setAttribute("active", "");
                    }
                    else {
                        button.removeAttribute("active");
                    }
                };
                optionButtonList.forEach((button) => {
                    const toggleData = button.getAttribute("gbs-toggle") ||
                        button.getAttribute("gbs-toggle-switch");
                    switch (toggleData) {
                        case "adcAutoGain":
                            toggleMethod(button, (optionByte0 & 0x01) == 0x01);
                            break;
                        case "scanlines":
                            toggleMethod(button, (optionByte0 & 0x02) == 0x02);
                            break;
                        case "vdsLineFilter":
                            toggleMethod(button, (optionByte0 & 0x04) == 0x04);
                            break;
                        case "peaking":
                            toggleMethod(button, (optionByte0 & 0x08) == 0x08);
                            break;
                        case "palForce60":
                            toggleMethod(button, (optionByte0 & 0x10) == 0x10);
                            break;
                        case "wantOutputComponent":
                            toggleMethod(button, (optionByte0 & 0x20) == 0x20);
                            break;
                        /** 1 */
                        case "matched":
                            toggleMethod(button, (optionByte1 & 0x01) == 0x01);
                            break;
                        case "frameTimeLock":
                            toggleMethod(button, (optionByte1 & 0x02) == 0x02);
                            break;
                        case "motionAdaptive":
                            toggleMethod(button, (optionByte1 & 0x04) == 0x04);
                            break;
                        case "bob":
                            toggleMethod(button, (optionByte1 & 0x04) != 0x04);
                            break;
                        // case "tap6":
                        //   toggleMethod(button, (optionByte1 & 0x08) != 0x04);
                        //   break;
                        case "step":
                            toggleMethod(button, (optionByte1 & 0x10) == 0x10);
                            break;
                        case "fullHeight":
                            toggleMethod(button, (optionByte1 & 0x20) == 0x20);
                            break;
                        /** 2 */
                        case "enableCalibrationADC":
                            toggleMethod(button, (optionByte2 & 0x01) == 0x01);
                            break;
                        case "preferScalingRgbhv":
                            toggleMethod(button, (optionByte2 & 0x02) == 0x02);
                            break;
                        case "disableExternalClockGenerator":
                            toggleMethod(button, (optionByte2 & 0x04) == 0x04);
                            break;
                        case "reverseRotaryEncoderForOledMenu":
                            toggleMethod(button, (optionByte2 & 0x08) == 0x08);
                            break;
                        case "ftlMethodVtotalVsst":
                            toggleMethod(button, (optionByte2 & 0x10) == 0);
                            break;
                        case "ftlMethodVtotalOnly":
                            toggleMethod(button, (optionByte2 & 0x10) == 0x10);
                            break;
                        case "outputRateFollow":
                            toggleMethod(button, (optionByte3 & 0x03) == 0);
                            break;
                        case "outputRate5994":
                            toggleMethod(button, (optionByte3 & 0x01) == 0x01);
                            break;
                        case "outputRate6000":
                            toggleMethod(button, (optionByte3 & 0x02) == 0x02);
                            break;
                    }
                });
                updateFtlMethodRowsVisibility((optionByte1 & 0x02) === 0x02);
            }
        }
    };
};
const checkReadyState = () => {
    if (GBSControl.ws.readyState == 2) {
        GBSControl.wsNoSuccessConnectingCounter++;
        if (GBSControl.wsNoSuccessConnectingCounter >= 7) {
            console.log("ws still closing, force close");
            GBSControl.ws = null;
            GBSControl.wsNoSuccessConnectingCounter = 0;
            /* fall through */
            createWebSocket();
            return false;
        }
        else {
            return true;
        }
    }
    else if (GBSControl.ws.readyState == 0) {
        GBSControl.wsNoSuccessConnectingCounter++;
        if (GBSControl.wsNoSuccessConnectingCounter >= 14) {
            console.log("ws still connecting, retry");
            GBSControl.ws.close();
            GBSControl.wsNoSuccessConnectingCounter = 0;
        }
        return true;
    }
    else {
        return true;
    }
};
const createIntervalChecks = () => {
    GBSControl.wsCheckTimer = setInterval(checkWebSocketServer, 500);
    GBSControl.updateTerminalTimer = setInterval(updateTerminal, 50);
};
/* API services */
const loadDoc = (link) => {
    return fetch(`http://${GBSControl.serverIP}/sc?${link}&nocache=${new Date().getTime()}`);
};
const loadUser = (link) => {
    if (link == "a" || link == "1") {
        GBSControl.isWsActive = false;
        GBSControl.ui.terminal.value += "\nRestart\n";
        GBSControl.ui.terminal.scrollTop = GBSControl.ui.terminal.scrollHeight;
    }
    return fetch(`http://${GBSControl.serverIP}/uc?${link}&nocache=${new Date().getTime()}`);
};
/** v54: white balance / colour cast buttons */
const loadImg = (code) => {
    return fetch(`http://${GBSControl.serverIP}/img?${code}&nocache=${new Date().getTime()}`);
};
/** SLOT management */
const savePreset = () => {
    const currentSlot = document.querySelector('[gbs-role="slot"][active]');
    if (!currentSlot) {
        return;
    }
    const key = currentSlot.getAttribute("gbs-element-ref");
    const currentIndex = currentSlot.getAttribute("gbs-slot-id");
    // v107l: saving over a slot that already holds a profile asks first
    const existing = slotDisplayName(currentSlot);
    const ask = existing
        ? gbsConfirm(`La ranura ya tiene el preset «${existing}». ¿Sobrescribirlo con lo que tienes ahora?`)
        : Promise.resolve();
    ask.then(() => gbsPrompt("Nombre del preset", existing || GBSControl.structs.slots[currentIndex].name || key))
        .then((currentName) => {
        if (currentName && currentName.trim() !== "Empty") {
            currentSlot.setAttribute("gbs-name", currentName);
            fetch(`/slot/save?index=${currentIndex}&name=${currentName.substring(0, 24)}&${+new Date()}`).then(() => {
                loadUser("4").then(() => {
                    setTimeout(() => {
                        fetchSlotNames().then((success) => {
                            if (success) {
                                updateSlotNames();
                            }
                        });
                    }, 500);
                });
            });
        }
    })
        .catch(() => { });
};
// v107l: deleting a slot asks first, naming it
const slotDisplayName = (slot) => {
    const n = slot ? (slot.getAttribute("gbs-name") || "").trim() : "";
    return n && n !== "Empty" ? n : "";
};
const removePreset = () => {
    const slot = document.querySelector('[gbs-role="slot"][active]');
    const name = slotDisplayName(slot);
    if (!slot || !name) {
        gbsAlert("Elige primero la ranura que quieres borrar.").catch(() => { });
        return;
    }
    gbsConfirm(`¿Borrar el preset «${name}»? No se puede deshacer.`).then(() => removePresetNow(), () => { });
};
const removePresetNow = () => {
    fetch(`/slot/remove?0&nocache=${new Date().getTime()}`).then(() => {
        setTimeout(() => {
            clearTimeout(GBSControl.wsTimeout);
            // GBSControl.wsTimeout = setTimeout(timeOutWs, 6000); //TODO: calc timeout
            fetch(`/slot/remove?1&nocache=${new Date().getTime()}`).then(() => {
                setTimeout(() => {
                    fetchSlotNames().then((success) => {
                        if (success) {
                            updateSlotNames();
                        }
                    });
                }, 500);
            });
        }, 200);
    });
};
const loadPreset = () => {
    loadUser("3").then(() => {
        // v107p: a saved profile is a whole snapshot, so it always brings its own filters back (the switch that made this
        // optional is gone)
        setTimeout(() => {
            fetch(`/gbs/restore-filters?${+new Date()}`);
        }, 250);
    });
};
const getSlotsHTML = () => {
    // prettier-ignore
    return [
        'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
        'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
        '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '-', '.', '_', '~', '(', ')', '!', '*', ':', ','
    ].map((chr, idx) => {
        return `<button
    class="gbs-button gbs-button__slot"
    gbs-slot-id="${idx}"
    gbs-message="${chr}"
    gbs-message-type="setSlot"
    gbs-click="normal"
    gbs-element-ref="slot-${chr}"
    gbs-meta="1024&#xa;x768"
    gbs-role="slot"
    gbs-name="slot-${idx}"
  ></button>`;
    }).join('');
};
const setSlot = (slot) => {
    fetch(`/slot/set?slot=${slot}&${+new Date()}`);
};
const updateSlotNames = () => {
    for (let i = 0; i < GBSControl.maxSlots; i++) {
        const el = document.querySelector(`[gbs-slot-id="${i}"]`);
        el.setAttribute("gbs-name", GBSControl.structs.slots[i].name);
        el.setAttribute("gbs-meta", getSlotPresetName(parseInt(GBSControl.structs.slots[i].presetID, 10)));
    }
};
const fetchSlotNames = () => {
    return fetch(`/bin/slots.bin?${+new Date()}`)
        .then((response) => response.arrayBuffer())
        .then((arrayBuffer) => {
        if (arrayBuffer.byteLength ===
            StructParser.getSize(Structs, "slots") * GBSControl.maxSlots) {
            GBSControl.structs = {
                slots: StructParser.parseStructArray(arrayBuffer, Structs, "slots"),
            };
            return true;
        }
        return false;
    });
};
const getSlotPresetName = (presetID) => {
    switch (presetID) {
        case 0x01:
        case 0x011:
            return "1280x960";
        case 0x02:
        case 0x012:
            return "1280x1024";
        case 0x03:
        case 0x013:
            return "1280x720";
        case 0x05:
        case 0x015:
            return "1920x1080";
        case 0x06:
        case 0x016:
            return "DOWNSCALE";
        case 0x04:
            return "720x480";
        case 0x14:
            return "768x576";
        case 0x21: // bypass 1
        case 0x22: // bypass 2
            return "BYPASS";
        default:
            return "CUSTOM";
    }
};
const fetchSlotNamesErrorRetry = () => {
    setTimeout(fetchSlotNamesAndInit, 1000);
};
const fetchSlotNamesAndInit = () => {
    fetchSlotNames()
        .then((success) => {
        if (!success) {
            fetchSlotNamesErrorRetry();
            return;
        }
        initUIElements();
        wifiGetStatus().then(() => {
            initUI();
            updateSlotNames();
            createWebSocket();
            createIntervalChecks();
            setTimeout(hideLoading, 1000);
        });
    }, fetchSlotNamesErrorRetry)
        .catch(fetchSlotNamesErrorRetry);
};
/** Promises */
const serial = (funcs) => funcs.reduce((promise, func) => promise.then((result) => func().then(Array.prototype.concat.bind(result))), Promise.resolve([]));
/** helpers */
const toggleHelp = () => {
    let help = GBSStorage.read("help") || false;
    GBSStorage.write("help", !help);
    updateHelp(!help);
};
const toggleDeveloperMode = () => {
    const developerMode = GBSStorage.read("developerMode") || false;
    GBSStorage.write("developerMode", !developerMode);
    updateDeveloperMode(!developerMode);
};
const toggleCustomSlotFilters = () => {
    const customSlotFilters = GBSStorage.read("customSlotFilters");
    GBSStorage.write("customSlotFilters", !customSlotFilters);
    updateCustomSlotFilters(!customSlotFilters);
};
const updateHelp = (help) => {
    if (help) {
        document.body.classList.remove("gbs-help-hide");
    }
    else {
        document.body.classList.add("gbs-help-hide");
    }
};
const updateDeveloperMode = (developerMode) => {
    const el = document.querySelector('[gbs-section="developer"]');
    if (developerMode) {
        el.removeAttribute("hidden");
        GBSControl.ui.developerSwitch.setAttribute("active", "");
        document.body.classList.remove("gbs-output-hide");
    }
    else {
        el.setAttribute("hidden", "");
        GBSControl.ui.developerSwitch.removeAttribute("active");
        document.body.classList.add("gbs-output-hide");
    }
    GBSControl.ui.developerSwitch.querySelector(".gbs-icon").innerText = developerMode ? "toggle_on" : "toggle_off";
};
const updateCustomSlotFilters = (customFilters = GBSStorage.read("customSlotFilters") === true) => {
    if (!GBSControl.ui.customSlotFilters)
        return; // v107p: the switch is gone
    if (customFilters) {
        GBSControl.ui.customSlotFilters.setAttribute("active", "");
    }
    else {
        GBSControl.ui.customSlotFilters.removeAttribute("active");
    }
    GBSControl.ui.customSlotFilters.querySelector(".gbs-icon").innerText = customFilters ? "toggle_on" : "toggle_off";
};
const GBSStorage = {
    lsObject: {},
    write(key, value) {
        GBSStorage.lsObject = GBSStorage.lsObject || {};
        GBSStorage.lsObject[key] = value;
        localStorage.setItem("GBSControlSlotNames", JSON.stringify(GBSStorage.lsObject));
    },
    read(key) {
        GBSStorage.lsObject = JSON.parse(localStorage.getItem("GBSControlSlotNames") || "{}");
        return GBSStorage.lsObject[key];
    },
};
const nodelistToArray = (nodelist) => {
    return Array.prototype.slice.call(nodelist);
};
// v61: mark the buttons of the active mode / width / fill state, read from the status bar line
const setButtonActive = (message, type, on) => {
    document
        .querySelectorAll(`.gbs-button[gbs-message="${message}"][gbs-message-type="${type}"]`)
        .forEach((button) => {
        if (on) {
            button.setAttribute("active", "");
        }
        else {
            button.removeAttribute("active");
        }
    });
};
let ourModeOn = false; // (after v107v) one of our modes is on screen (see markActiveModes)
const markActiveModes = (text) => {
    // v90: the status bar is three lines now, so split on the newlines as well as on the pipes, or the last field of one line
    // and the first of the next arrive glued together and nothing matches.
    const parts = text.split(/\n| \| /).map((part) => part.trim());
    const has = (name) => parts.indexOf(name) >= 0;
    // v90: a prefix test, because the step field carries its name with it ("step 2/4 CRT"). v88 compared the whole field
    // against "step 2/4" and never matched, so the 256 / 320 buttons stopped lighting up in PAL.
    const starts = (prefix) => parts.some((part) => part.indexOf(prefix) === 0);
    // v90: the aspect is its own field now, so "720p x3" is true in both 4:3 and 16:9 and has to be qualified
    const wide169 = has("16:9");
    setButtonActive("!", "user", has("1080p x4"));
    setButtonActive("9", "user", has("1080p x5"));
    setButtonActive("^", "user", has("720p x3") && !wide169);
    setButtonActive("10", "img", has("720p x3") && wide169);
    setButtonActive("|", "user", has("720p x4"));
    setButtonActive(">", "user", has("720p x5"));
    const anyMode = has("1080p x4") ||
        has("1080p x5") ||
        has("720p x3") ||
        has("720p x4") ||
        has("720p x5");
    // v88: in the PAL presets the status bar names the size step ("step 2/4 CRT") instead of 256 px / 320 px, because the two
    // width buttons there are shortcuts into the step cycle. Light the same buttons from the step: 256 px is step 1, 320 px is
    // step 2, and the fill button is lit on the two steps that only it can reach.
    setButtonActive("(", "user", anyMode && (has("320 px") || starts("step 2/")));
    setButtonActive(")", "user", anyMode && (has("256 px") || starts("step 1/")));
    setButtonActive("<", "user", anyMode && (has("wide") || starts("step 3/4") || starts("step 4/4")));
    // (after v107v) his request: with one of our modes on, nothing is lit among Rama's original resolutions (ours sits on top
    // of one of them, and seeing both lit was confusing). The "#" message would light it again, so it reads this flag.
    ourModeOn = anyMode || has("960p x4") || has("480p x2");
    if (ourModeOn) {
        nodelistToArray(document.querySelectorAll("[gbs-role='preset']")).forEach((b) => {
            if (b.getAttribute("gbs-element-ref") !== "buttonLoadCustomPreset")
                b.removeAttribute("active");
        });
    }
    setButtonActive("23", "img", has("tol.sync")); // v63
    setButtonActive("m", "action", has("watcher off")); // v64: the watcher is stopped
    setButtonActive("25", "img", has("retrying")); // v66: hands-free attempts running
};
const toggleButtonActive = (id) => (button, _index, _array) => {
    button.removeAttribute("active");
    if (button.getAttribute("gbs-element-ref") === id) {
        button.setAttribute("active", "");
    }
};
const displayWifiWarning = (mode) => {
    GBSControl.ui.webSocketConnectionWarning.style.display = mode
        ? "block"
        : "none";
};
const updateTerminal = () => {
    if (GBSControl.queuedText.length > 0) {
        requestAnimationFrame(() => {
            GBSControl.ui.terminal.value += GBSControl.queuedText;
            GBSControl.ui.terminal.scrollTop = GBSControl.ui.terminal.scrollHeight;
            GBSControl.queuedText = "";
        });
    }
};
const updateViewPort = () => {
    document.documentElement.style.setProperty("--viewport-height", window.innerHeight + "px");
};
const hideLoading = () => {
    GBSControl.ui.loader.setAttribute("style", "display:none");
};
const checkFetchResponseStatus = (response) => {
    if (!response.ok) {
        throw new Error(`HTTP ${response.status} - ${response.statusText}`);
    }
    return response;
};
const readLocalFile = (file) => {
    const reader = new FileReader();
    reader.addEventListener("load", (event) => {
        doRestore(reader.result);
    });
    reader.readAsArrayBuffer(file);
};
/** backup / restore */
const doBackup = () => {
    let backupFiles;
    let done = 0;
    let total = 0;
    fetch("/filesystem/dir")
        .then((r) => r.json())
        .then((files) => {
        backupFiles = files;
        total = files.length;
        const funcs = files.map((path) => () => {
            return fetch(`/filesystem/download?file=${path}&${+new Date()}`).then((response) => {
                GBSControl.ui.progressBackup.setAttribute("gbs-progress", `${done}/${total}`);
                done++;
                return checkFetchResponseStatus(response) && response.arrayBuffer();
            });
        });
        return serial(funcs);
    })
        .then((files) => {
        const headerDescriptor = files.reduce((acc, f, index) => {
            acc[backupFiles[index]] = f.byteLength;
            return acc;
        }, {});
        const backupFilesJSON = JSON.stringify(headerDescriptor);
        const backupFilesJSONSize = backupFilesJSON.length;
        const mainHeader = [
            (backupFilesJSONSize >> 24) & 255, // size
            (backupFilesJSONSize >> 16) & 255, // size
            (backupFilesJSONSize >> 8) & 255, // size
            (backupFilesJSONSize >> 0) & 255,
        ];
        const outputArray = [
            ...mainHeader,
            ...backupFilesJSON.split("").map((c) => c.charCodeAt(0)),
            ...files.reduce((acc, f, index) => {
                acc = acc.concat(Array.from(new Uint8Array(f)));
                return acc;
            }, []),
        ];
        downloadBlob(new Blob([new Uint8Array(outputArray)]), `gbs-control.backup-${+new Date()}.bin`);
        GBSControl.ui.progressBackup.setAttribute("gbs-progress", ``);
    });
};
const doRestore = (file) => {
    const { backupInput } = GBSControl.ui;
    const fileBuffer = new Uint8Array(file);
    const headerCheck = fileBuffer.slice(4, 6);
    if (headerCheck[0] !== 0x7b || headerCheck[1] !== 0x22) {
        backupInput.setAttribute("disabled", "");
        gbsAlert("El archivo de copia no es válido.")
            .then(() => {
            backupInput.removeAttribute("disabled");
        }, () => {
            backupInput.removeAttribute("disabled");
        })
            .catch(() => {
            backupInput.removeAttribute("disabled");
        });
        return;
    }
    const b0 = fileBuffer[0], b1 = fileBuffer[1], b2 = fileBuffer[2], b3 = fileBuffer[3];
    const headerSize = (b0 << 24) + (b1 << 16) + (b2 << 8) + b3;
    const headerString = Array.from(fileBuffer.slice(4, headerSize + 4))
        .map((c) => String.fromCharCode(c))
        .join("");
    const headerObject = JSON.parse(headerString);
    const files = Object.keys(headerObject);
    let pos = headerSize + 4;
    let total = files.length;
    let done = 0;
    const funcs = files.map((fileName) => () => {
        const fileContents = fileBuffer.slice(pos, pos + headerObject[fileName]);
        const formData = new FormData();
        const uploadName = fileName.startsWith("/") ? fileName.substr(1) : fileName;
        formData.append("file", new Blob([fileContents], { type: "application/octet-stream" }), uploadName);
        return fetch("/filesystem/upload", {
            method: "POST",
            body: formData,
        }).then((response) => {
            GBSControl.ui.progressRestore.setAttribute("gbs-progress", `${done}/${total}`);
            done++;
            pos += headerObject[fileName];
            return response;
        });
    });
    serial(funcs).then(() => {
        GBSControl.ui.progressRestore.setAttribute("gbs-progress", ``);
        loadUser("a").then(() => {
            gbsAlert("Reiniciando GBS-Control.\nEspera a que vuelva la conexión wifi y pulsa OK.")
                .then(() => {
                window.location.reload();
            })
                .catch(() => { });
        });
    });
};
const downloadBlob = (blob, name = "file.txt") => {
    // Convert your blob into a Blob URL (a special url that points to an object in the browser's memory)
    const blobUrl = URL.createObjectURL(blob);
    // Create a link element
    const link = document.createElement("a");
    // Set link's href to point to the Blob URL
    link.href = blobUrl;
    link.download = name;
    // Append link to the body
    document.body.appendChild(link);
    // Dispatch click event on the link
    // This is necessary as link.click() does not work on the latest firefox
    link.dispatchEvent(new MouseEvent("click", {
        bubbles: true,
        cancelable: true,
        view: window,
    }));
    // Remove link from body
    document.body.removeChild(link);
};
/** WIFI management */
const wifiGetStatus = () => {
    return fetch(`/wifi/status?${+new Date()}`)
        .then((r) => r.json())
        .then((wifiStatus) => {
        GBSControl.wifi = wifiStatus;
        if (GBSControl.wifi.mode === "ap") {
            GBSControl.ui.wifiApButton.setAttribute("active", "");
            GBSControl.ui.wifiApButton.classList.add("gbs-button__secondary");
            GBSControl.ui.wifiStaButton.removeAttribute("active", "");
            GBSControl.ui.wifiStaButton.classList.remove("gbs-button__secondary");
            GBSControl.ui.wifiStaSSID.innerHTML = "Buscar redes";
        }
        else {
            GBSControl.ui.wifiApButton.removeAttribute("active", "");
            GBSControl.ui.wifiApButton.classList.remove("gbs-button__secondary");
            GBSControl.ui.wifiStaButton.setAttribute("active", "");
            GBSControl.ui.wifiStaButton.classList.add("gbs-button__secondary");
            GBSControl.ui.wifiStaSSID.innerHTML = `${GBSControl.wifi.ssid}`;
        }
    });
};
const wifiConnect = () => {
    const ssid = GBSControl.ui.wifiSSDInput.value;
    const password = GBSControl.ui.wifiPasswordInput.value;
    if (!password.length) {
        GBSControl.ui.wifiPasswordInput.classList.add("gbs-wifi__input--error");
        return;
    }
    const formData = new FormData();
    formData.append("n", ssid);
    formData.append("p", password);
    fetch("/wifi/connect", {
        method: "POST",
        body: formData,
    }).then(() => {
        gbsAlert(`GBS-Control se reiniciará y se conectará a ${ssid}. Espera unos segundos y pulsa OK.`)
            .then(() => {
            window.location.href = "http://gbscontrol.local/";
        })
            .catch(() => { });
    });
};
const wifiScanSSID = () => {
    GBSControl.ui.wifiStaButton.setAttribute("disabled", "");
    GBSControl.ui.wifiListTable.innerHTML = "";
    GBSControl.ui.wifiList.removeAttribute("hidden");
    GBSControl.ui.wifiConnect.setAttribute("hidden", "");
    const showWiFiStatusRow = (text) => {
        GBSControl.ui.wifiListTable.innerHTML = `<tr><td colspan="3" style="text-align:center;opacity:.8">${text}</td></tr>`;
    };
    if (!GBSControl.scanSSIDDone) {
        GBSControl.scanSSIDRetries = 0;
        showWiFiStatusRow("Scanning...");
        fetch(`/wifi/list?${+new Date()}`).then(() => {
            GBSControl.scanSSIDDone = true;
            setTimeout(wifiScanSSID, 3000);
        });
        return;
    }
    fetch(`/wifi/list?${+new Date()}`)
        .then((e) => e.text())
        .then((result) => {
        // Some scans can take longer than 3s; keep polling a few times if empty.
        if (!result.length && GBSControl.scanSSIDRetries < 10) {
            GBSControl.scanSSIDRetries++;
            showWiFiStatusRow("Scanning...");
            setTimeout(wifiScanSSID, 1000);
            return null;
        }
        GBSControl.scanSSIDDone = false;
        return result.length
            ? result
                .split("\n")
                .map((line) => line.split(","))
                .map(([strength, encripted, ssid]) => {
                return { strength, encripted, ssid };
            })
            : [];
    })
        .then((ssids) => {
        if (ssids === null) {
            return null;
        }
        return ssids.reduce((acc, ssid) => {
            return `${acc}<tr gbs-ssid="${ssid.ssid}">
        <td class="gbs-icon" style="opacity:${parseInt(ssid.strength, 10) / 100}">wifi</td>
        <td>${ssid.ssid}</td>
        <td class="gbs-icon">${ssid.encripted ? "lock" : "lock_open"}</td>
      </tr>`;
        }, "");
    })
        .then((html) => {
        if (html === null) {
            return;
        }
        GBSControl.ui.wifiStaButton.removeAttribute("disabled");
        if (html.length) {
            GBSControl.ui.wifiListTable.innerHTML = html;
            GBSControl.ui.wifiList.removeAttribute("hidden");
            GBSControl.ui.wifiConnect.setAttribute("hidden", "");
        }
        else {
            GBSControl.ui.wifiListTable.innerHTML = `<tr><td colspan="3" style="text-align:center;opacity:.8">No hay redes a la vista</td></tr>`;
        }
    });
};
const wifiSelectSSID = (event) => {
    GBSControl.ui
        .wifiSSDInput.value = event.target.parentElement.getAttribute("gbs-ssid");
    GBSControl.ui.wifiPasswordInput.classList.remove("gbs-wifi__input--error");
    GBSControl.ui.wifiList.setAttribute("hidden", "");
    GBSControl.ui.wifiConnect.removeAttribute("hidden");
};
const wifiSetAPMode = () => {
    if (GBSControl.wifi.mode === "ap") {
        return;
    }
    const formData = new FormData();
    formData.append("n", "dummy");
    fetch("/wifi/connect", {
        method: "POST",
        body: formData,
    }).then(() => {
        gbsAlert("Cambiando a punto de acceso. Conéctate a la red «gbscontrol» y pulsa OK.")
            .then(() => {
            window.location.href = "http://192.168.4.1";
        })
            .catch(() => { });
    });
};
/** button click management */
const controlClick = (control) => () => {
    const controlKey = control.getAttribute("gbs-control-key");
    const target = GBSControl.controlKeysMobile[GBSControl.controlKeysMobileMode];
    switch (target.type) {
        case "loadDoc":
            loadDoc(target[controlKey]);
            break;
        case "loadUser":
            loadUser(target[controlKey]);
            break;
    }
};
/** [patch touch] press-and-repeat for buttons. On touch screens it ignores scroll gestures:
 *  a short tap fires once on release, holding for 350 ms starts repeating, moving the finger cancels. */
const bindPressRepeat = (el, callback) => {
    const stop = () => {
        clearTimeout(el["__holdTimer"]);
        clearInterval(el["__interval"]);
        el["__holdTimer"] = null;
    };
    if (!("ontouchstart" in window)) {
        el.addEventListener("mousedown", () => {
            stop();
            callback();
            el["__interval"] = setInterval(callback, 300);
        });
        el.addEventListener("mouseup", stop);
        el.addEventListener("mouseleave", stop);
        return;
    }
    let startX = 0;
    let startY = 0;
    el.addEventListener("touchstart", (e) => {
        stop();
        const t = e.touches[0];
        startX = t.clientX;
        startY = t.clientY;
        el["__holdTimer"] = setTimeout(() => {
            el["__holdTimer"] = null;
            callback();
            el["__interval"] = setInterval(callback, 300);
        }, 350);
    }, { passive: true });
    el.addEventListener("touchmove", (e) => {
        const t = e.touches[0];
        if (Math.abs(t.clientX - startX) > 8 || Math.abs(t.clientY - startY) > 8) {
            stop();
        }
    }, { passive: true });
    el.addEventListener("touchend", () => {
        const wasTap = el["__holdTimer"] != null;
        stop();
        if (wasTap) {
            callback();
        }
    });
    el.addEventListener("touchcancel", stop);
};
const PAD_KEYS = {
    move: {
        up: { msg: "*", type: "action" }, // shiftVerticalUpIF
        down: { msg: "/", type: "action" }, // shiftVerticalDownIF
        left: { msg: "7", type: "action" }, // moveCanvasHorizontal(false)
        right: { msg: "6", type: "action" }, // moveCanvasHorizontal(true)
    },
    crop: {
        up: { msg: "*", type: "user" }, // top edge in
        down: { msg: ";", type: "user" }, // bottom edge in
        left: { msg: "-", type: "user" }, // left edge in
        right: { msg: ".", type: "user" }, // right edge in
    },
    scale: {
        up: { msg: "4", type: "action" }, // scale vertical +
        down: { msg: "5", type: "action" }, // scale vertical -
        left: { msg: "h", type: "action" }, // scale horizontal -
        right: { msg: "z", type: "action" }, // scale horizontal +
    },
};
const PAD_CROP_OUT = { up: ",", down: "$", left: "_", right: "~" };
// what each mode is for, in two short lines. The long versions are the ones the firmware prints in the log.
// v107f: the description is built from three parts, because the size step changes what is true about the picture:
// what is seen vertically (the mode), how wide it is (the step), and which screen it is for (the mode and shape).
const WORK_DESC = {
    "720p x3 4:3": ["", // (after v107v) his decision: the "224 lines" sentence is not needed here
        "Para pantalla 4:3 o 16:9 a elección del usuario."],
    "720p x3 16:9": ["", // (after v107v) his decision: the "224 lines" sentence is not needed here
        "Para pantalla 4:3 o 16:9 a elección del usuario."],
    "720p x4 16:9": ["Recomendado para Super Game Boy y juegos con ventanas reducidas. Se ven 180 de las 224 líneas.", "Para una pantalla 16:9."],
    "720p x5 16:9": ["Experimental. Recomendado para Super Game Boy y juegos con ventanas muy pequeñas. Se ven 144 de las 224 líneas: Game Boy a pantalla completa.", "Para una pantalla 16:9."],
    "1080p x4": ["", // (after v107v) his decision: the "224 lines" sentence is not needed here
        "Para una pantalla 16:9 a 1080p."],
    "1080p x5": ["Se ven 216 de las 224 líneas: faltan 4 arriba y 4 abajo, que en muchos juegos apenas se notan.", "Para una pantalla 16:9 a 1080p."],
};
// v107n: with any console other than the SNES the Super Game Boy means nothing, so x4 and x5 have their own sentence
const WORK_DESC_OTHER = {
    "720p x4 16:9": ["Recomendado para juegos con ventanas pequeñas. Se ven 180 de las 224 líneas.", "Para una pantalla 16:9."],
    "720p x5 16:9": ["Experimental. Recomendado para juegos con ventanas muy pequeñas. Se ven 144 de las 224 líneas.", "Para una pantalla 16:9."],
};
// v107n: the Líneas row (his texts). 1 is 240 with the Mega Drive and 239 with the SNES.
const LINES_NOTE_224 = "Se ven las 224 líneas que usan casi todos los juegos. Un borde tapa la basura, ajústalo manualmente si se come algunos píxeles del juego o está desplazado.";
const LINES_NOTE_240 = "Se muestran hasta 240 líneas. Juegos no optimizados se ven al completo."; // (after v107q) his text
const LINES_NOTE_239 = "Se ven todas las líneas, ya sean 224 o 239."; // v107p: his text for the SNES "224 / 239" button
const LINES_NOTE_DOLOR = "Franjas, marcos de colorines y achatado. Así solía ser el formato PAL. Scanlines no disponibles en este modo.";
const LINES_NOTE_DOLOR_SNES = "Franjas y achatado, así solía ser el formato PAL. Scanlines no disponibles en este modo."; // v107p: his text
// the row, from "ln=" (0 224, 1 240 / 239, 2 DOLOR) and "lna=" (bit 0 row, bit 1 DOLOR) of the data line. DOLOR also
// switches the scanlines off (the firmware refuses them), so their controls in Filtros are locked meanwhile, and the size
// buttons do nothing in DOLOR, which has one placement of its own, so they are locked too.
const paintLines = (raw, s) => {
    const lnM = /\bln=(\d+)/.exec(raw);
    const laM = /\blna=(\d+)/.exec(raw);
    const ln = lnM ? parseInt(lnM[1], 10) : 0;
    const lna = laM ? parseInt(laM[1], 10) : 0;
    const row = !!s.mode && (lna & 1) === 1;
    const dolor = row && ln === 2;
    const box = document.getElementById("workLines");
    if (box) {
        row ? box.removeAttribute("hidden") : box.setAttribute("hidden", "");
        const n240 = document.getElementById("lines240");
        // v107p: with the SNES the 224 and 239 buttons are one, "224 / 239": its window is always open to the 239 lines
        if (n240)
            n240.textContent = s.snes ? "224 / 239" : "240";
        nodelistToArray(box.querySelectorAll("[data-lines]")).forEach((c) => {
            const n = parseInt(c.getAttribute("data-lines") || "0", 10);
            // v107p: no 224 with the SNES (bit 2): its games have no frame, so the window is always open to 239
            // (after v107q) and no 224 at all any more: the row is 240 and DOLOR (his decision of 2026-11-13)
            (n === 2 && !(lna & 2)) || n === 0 ? c.setAttribute("hidden", "") : c.removeAttribute("hidden");
            n === ln ? c.setAttribute("active", "") : c.removeAttribute("active");
        });
        const note = document.getElementById("workLinesNote");
        if (note)
            note.textContent = ln === 2 ? (s.snes ? LINES_NOTE_DOLOR_SNES : LINES_NOTE_DOLOR) : ln === 1 ? (s.snes ? LINES_NOTE_239 : LINES_NOTE_240) : LINES_NOTE_224;
    }
    nodelistToArray(document.querySelectorAll("#workSteps [data-step]")).forEach((c) => {
        dolor ? c.setAttribute("disabled", "") : c.removeAttribute("disabled");
    });
    const slNote = document.getElementById("slDolor");
    if (slNote)
        dolor ? slNote.removeAttribute("hidden") : slNote.setAttribute("hidden", "");
    const slSwitch = document.getElementById("slSwitch");
    if (slSwitch)
        dolor ? slSwitch.setAttribute("disabled", "") : slSwitch.removeAttribute("disabled");
    nodelistToArray(document.querySelectorAll("#slRow button")).forEach((b) => {
        dolor ? b.setAttribute("disabled", "") : b.removeAttribute("disabled");
    });
    return { row, dolor };
};
const WORK_WIDTH_SNESCRT = "Proporción con la que jugábamos a SNES en tele de tubo. Píxel horizontal no entero.";
const WORK_WIDTH_MD256 = "Píxel cuadrado en juegos de 256px, probablemente en MD te interese más el modo 320px.";
const WORK_WIDTH_PCE256 = "Píxel perfecto en juegos 256px de PCE.";
const WORK_WIDTH_PCECRT = "Proporción con la que jugábamos a PCE en tele de tubo. Píxel horizontal no entero.";
const WORK_WIDTH_MD320 = "Píxel perfecto en juegos 320px de MD, mismas proporciones que en tele de tubo.";
// the width, by size step: 0 square (256 px), 1 CRT (320 px), 2 wide, 3 fill
const WORK_WIDTH = [
    "Píxeles enteros y del mismo tamaño.",
    "Forma de tele de tubo: más ancha, el píxel ya no es entero en horizontal (en vertical sí).",
    // v107m: steps 3 and 4 are the "ancho" and "llenar" buttons, one description for both (his text)
    "Este modo llena más pantalla, deforma el aspecto. Sólo para experimentar.",
    "Este modo llena más pantalla, deforma el aspecto. Sólo para experimentar.",
];
// (after v107z) THE DREAMCAST'S OWN MODES. "dc=" on the data line says what it is sending while its profile is in use
// (1 240p, 2 288p, 3 480i, 4 576i); only the modes measured for that signal are offered, with their own description.
let dcNow = 0;
let dcAspect1080 = false; // the Dreamcast's 1080p in 480i is on screen: its 4:3 / 16:9 go to code 74
const DC_MODES = {
    1: ["720p x3", "1080p x4"], // (after v108f) his decision: the 240p 1080p x5 stays in the firmware (its measures and text) but is never shown
    2: ["720p x3", "1080p x4"],
    3: ["720p x3", "1080p x4"],
    4: ["720p x3", "1080p x4"],
    5: ["720p x3", "1080p x4"], // (after v108b) the Dreamcast through VGA: Rama's presets with its measured placements on top
};
const DC_DESC = {
    // (after v108f) his simpler texts; for 480i/576i/VGA the data aspect 16:9 is «Normal» and 4:3 is «Panorámico»
    "1 720p x3 16:9": "Píxel perfecto con toda la imagen. Para una tele 16:9.",
    "1 720p x3 4:3": "Píxel perfecto a todo lo ancho. Para una tele puesta en modo 4:3.",
    "1 1080p x4": "Píxel perfecto con toda la imagen.",
    "1 1080p x5": "Experimental. Píxel perfecto más grande: se pierde un poco de imagen arriba y abajo.",
    "2 720p x3 16:9": "Píxel perfecto. Se pierde un poco de imagen arriba y abajo. Para una tele 16:9.",
    "2 720p x3 4:3": "Píxel perfecto a todo lo ancho. Se pierde un poco de imagen arriba y abajo. Para una tele puesta en modo 4:3.",
    "2 1080p x4": "Píxel perfecto con toda la imagen.",
    "3 1080p x4 16:9": "Para la mayoría de juegos de Dreamcast. Imagen nítida y proporciones correctas.",
    "3 1080p x4 4:3": "Para el modo panorámico de juegos como Rayman 2, F355, MSR... La imagen llenará el ancho de tu pantalla 16:9.",
    "3 720p x3 16:9": "Para la mayoría de juegos de Dreamcast. Imagen nítida y proporciones correctas.",
    "3 720p x3 4:3": "Para el modo panorámico de juegos como Rayman 2, F355, MSR... La imagen llenará el ancho de tu pantalla 16:9.",
    "4 1080p x4 16:9": "Para la mayoría de juegos de Dreamcast. Imagen nítida y proporciones correctas.",
    "4 1080p x4 4:3": "Para el modo panorámico de juegos como Rayman 2, F355, MSR... La imagen llenará el ancho de tu pantalla 16:9.",
    "4 720p x3 16:9": "Para la mayoría de juegos de Dreamcast. Imagen nítida y proporciones correctas.",
    "4 720p x3 4:3": "Para el modo panorámico de juegos como Rayman 2, F355, MSR... La imagen llenará el ancho de tu pantalla 16:9.",
    "5 1080p x4 16:9": "Para la mayoría de juegos de Dreamcast. Imagen nítida y proporciones correctas.",
    "5 1080p x4 4:3": "Para el modo panorámico de juegos como Rayman 2, F355, MSR... La imagen llenará el ancho de tu pantalla 16:9.",
    "5 720p x3 16:9": "Para la mayoría de juegos de Dreamcast. Imagen nítida y proporciones correctas.",
    "5 720p x3 4:3": "Para el modo panorámico de juegos como Rayman 2, F355, MSR... La imagen llenará el ancho de tu pantalla 16:9.",
};
// modes that crop the picture vertically, and so need the hint about moving it
const WORK_CROPS = { "720p x4 16:9": true, "720p x5 16:9": true, "1080p x5": true };
// the status bar is written for the log, in English; on the screen it is shown in Spanish
// (after v107v) his list of 2026-11-14: console profiles without measurements (firmware profiles 4..17, in this order,
// web codes 60..73). The status bar prints these names as they are; their placement is the Mega Drive's, so the width
// buttons treat them as "MD".
// No TurboGrafx-16 (his decision): it is the PC Engine itself.
const EMPTY_PROFILES = ["PlayStation", "Saturn", "Nintendo 64", "NES", "Master System", "Neo Geo AES",
    "Neo Geo CD", "3DO", "Jaguar", "Dreamcast", "PlayStation 2", "Xbox", "GameCube", "Wii"];
// the profile in the bar: "profile MD", "profile auto SNES", "profile Neo Geo CD"
// (after v108e) the profiles that keep only colour, filters, edges and last state: the Dreamcast is measured now (its button
// is next to the Mega Drive's), and «Otras» is his choice for any console not in the list, with its own text
const UNMEASURED = EMPTY_PROFILES.filter((n) => n !== "Dreamcast").concat(["Others"]); // (after v108f) the bar names it in English, «Otras» only on the screen
const CONSOLE_DESC_OTHERS = "¿No encuentras tu consola? Usa esta opción: guarda el color, filtros, bordes y estados del último uso. Si la imagen no encaja, prueba los presets originales de Rama en la pestaña Modo. Facilitar tus mediciones y la huella de tu sistema ayudará a incluirla en el próximo firmware.";
const barProfile = (text) => {
    const m = /profile (auto )?([^|\n]+)/.exec(text || "");
    return m ? { auto: !!m[1], name: m[2].trim() } : null;
};
const CONSOLE_DESC = "Elige el sistema que vas a usar para que se muestre lo mejor posible. Actualmente el firmware sólo detecta y está probado con unos pocos.";
// (after v107v) his text of 2026-11-14 for the profiles without measurements
const CONSOLE_DESC_EMPTY = "Perfil sin mediciones, de momento sólo guarda el color, filtros, bordes y estados del último uso. Si sabes calcular el píxel perfecto para este sistema, ponte en contacto con Alexei. Facilitar tus mediciones y la huella de tu sistema, ayudará a incluirlo en el próximo firmware.";
const translateStatus = (text) => text
    .replace(/^detected not yet/m, "consola sin detectar")
    .replace(/^detected /m, "")
    .replace(/profile auto /g, "perfil auto ")
    .replace(/profile /g, "perfil ")
    .replace(/\bperfil (auto )?Others\b/, "perfil $1Otras") // (after v108f) the bar says "Others"; on the screen it is «Otras»
    .replace(/stock preset/g, "preset de fábrica")
    .replace(/watcher off/g, "vigilante parado")
    .replace(/retrying/g, "reintentando")
    .replace(/losses (\d+) retries (\d+)/g, "pérdidas $1, reintentos $2")
    .replace(/step (\d)\/(\d) square/g, "paso $1 de $2, cuadrado")
    .replace(/step (\d)\/(\d) CRT/g, "paso $1 de $2, CRT")
    .replace(/step (\d)\/(\d) wide/g, "paso $1 de $2, ancho")
    .replace(/step (\d)\/(\d) fill/g, "paso $1 de $2, llenar")
    .replace(/\| wide$/m, "| ancho")
    .replace(/\| fine$/m, "| ajuste fino")
    // (after v108e) the Dreamcast in 480i / 576i / VGA: no multiplier on its modes (2 and 1.5 lines), as on its buttons
    .replace(/^(720p) x3( \| [^\n]*\| dc (?:480i|576i|vga))/m, "$1$2")
    .replace(/^(1080p) x4( \| [^\n]*\| dc (?:480i|576i|vga))/m, "$1$2");
const parseWork = (text) => {
    const lines = text.split("\n");
    const last = (lines[2] || "").split(" | ").map((p) => p.trim());
    const mode = /^(720p x[345]|1080p x[45])$/.test(last[0] || "") ? last[0] : "";
    const aspect = last.indexOf("16:9") >= 0 ? "16:9" : last.indexOf("4:3") >= 0 ? "4:3" : "";
    let step = -1;
    let steps = 0;
    last.forEach((p) => {
        const m = /^step (\d)\/(\d)/.exec(p);
        if (m) {
            step = parseInt(m[1], 10) - 1;
            steps = parseInt(m[2], 10);
        }
        if (p === "256 px")
            step = 0;
        if (p === "320 px")
            step = 1;
        if (p === "wide")
            step = 2;
    });
    // the profile in use, from the second line: "profile MD" or "profile auto MD"
    const bp = barProfile(lines[1] || "");
    const md = /profile (auto )?MD\b/.test(lines[1] || "") || (!!bp && (EMPTY_PROFILES.indexOf(bp.name) >= 0 || bp.name === "Others"));
    const snes = /profile (auto )?SNES\b/.test(lines[1] || "");
    const pce = /profile (auto )?PC-Engine\b/.test(lines[1] || "");
    return { mode, aspect, step, steps, key: mode.indexOf("720p") === 0 ? `${mode} ${aspect}` : mode, md, snes, pce, fine: last.indexOf("fine") >= 0 };
};
// what the pad has done since the mode was last placed
const work = {
    moveX: 0, moveY: 0, crop: 0, scale: 0,
    dirty: false,
    lastKey: "",
};
const describeMoves = () => {
    const parts = [];
    const steps = (n) => `${n} ${n === 1 ? "paso" : "pasos"}`;
    if (work.moveX)
        parts.push(`${steps(Math.abs(work.moveX))} a la ${work.moveX < 0 ? "izquierda" : "derecha"}`);
    if (work.moveY)
        parts.push(`${steps(Math.abs(work.moveY))} ${work.moveY < 0 ? "arriba" : "abajo"}`);
    let text = parts.length ? "Movida " + parts.join(" y ") : "";
    const extra = [];
    if (work.crop)
        extra.push(`${work.crop} ${work.crop === 1 ? "toque" : "toques"} de borde`);
    if (work.scale)
        extra.push(`${work.scale} ${work.scale === 1 ? "toque" : "toques"} de escala`);
    if (extra.length)
        text += (text ? ", " : "Retocada: ") + extra.join(", ");
    return text || "Tal como la midió el firmware";
};
const paintWork = () => {
    const remind = document.getElementById("workRemind");
    if (remind)
        work.dirty ? remind.setAttribute("dirty", "") : remind.removeAttribute("dirty");
    const moved = document.getElementById("workMoved");
    const saved = document.getElementById("workSaved");
    if (moved)
        moved.textContent = describeMoves();
    if (saved) {
        if (work.dirty) {
            saved.removeAttribute("hidden");
            saved.textContent = "sin guardar";
            saved.removeAttribute("ok");
        }
        else if (saved.hasAttribute("ok")) {
            saved.removeAttribute("hidden");
        }
        else {
            saved.setAttribute("hidden", "");
        }
    }
};
// (after v107r) each undo button clears only its own counter in the Colocación summary
let customPresetOn = false; // (after v107r) "cp=1" in the data line
const resetWorkPart = (part) => {
    if (part === "move")
        work.moveX = work.moveY = 0;
    if (part === "edge")
        work.crop = 0;
    if (part === "scale")
        work.scale = 0;
    work.dirty = !!(work.moveX || work.moveY || work.crop || work.scale);
    paintWork();
};
const resetWork = () => {
    work.moveX = work.moveY = work.crop = work.scale = 0;
    work.dirty = false;
    const saved = document.getElementById("workSaved");
    if (saved)
        saved.removeAttribute("ok");
    paintWork();
};
// called from the websocket every time the status bar arrives
const updateWorkScreen = (raw) => {
    customPresetOn = /\bcp=1\b/.test(raw);
    const bar = document.getElementById("gbsStatusBar");
    if (bar) {
        // the dot in front of the console line, green when something is detected
        const esc = (x) => x.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
        // v107j: the fourth line ("mem <MHz> <phase>") is data for the memory buttons, not for the bar
        const t = translateStatus(raw).split("\n").filter((l) => !/^mem \d+ \d+/.test(l) && !/^v /.test(l)).map(esc);
        if (t[1] !== undefined) {
            const known = t[1].indexOf("sin detectar") < 0;
            t[1] = `<span class="gbs-dot${known ? " gbs-dot--on" : ""}">●</span> ` + t[1];
        }
        bar.innerHTML = t.join("\n");
    }
    const s = parseWork(raw);
    const dcM = /\bdc=(\d)/.exec(raw);
    dcNow = dcM ? parseInt(dcM[1], 10) : 0;
    const dcModes = DC_MODES[dcNow];
    // the mode buttons: only the measured ones for this signal, and in 480i / 576i the 1080p is one button, «1080p»
    nodelistToArray(document.querySelectorAll("#workModes [data-mode]")).forEach((c) => {
        const m = c.getAttribute("data-mode") || "";
        dcModes && dcModes.indexOf(m) < 0 ? c.setAttribute("hidden", "") : c.removeAttribute("hidden");
        // (after v108d) his decision: in 480i / 576i / VGA neither multiplier is real (2 and 1.5 lines), so plain «720p» / «1080p»
        if (m === "1080p x4")
            c.textContent = dcNow >= 3 && dcNow <= 5 ? "1080p" : "1080p x4";
        if (m === "720p x3")
            c.textContent = dcNow >= 3 && dcNow <= 5 ? "720p" : "720p x3";
        if (m === "1080p x5")
            c.innerHTML = dcNow === 1 ? "<span>1080p x5</span><small>experimental</small>" : "1080p x5";
    });
    if (s.key !== work.lastKey) {
        work.lastKey = s.key;
        resetWork(); // a new mode is placed from its own measurements: nothing has been moved yet
    }
    const title = document.getElementById("workMode");
    const aspect = document.getElementById("workAspect");
    const desc = document.getElementById("workDesc");
    const notes = document.getElementById("workNotes");
    if (!title || !aspect || !desc || !notes)
        return;
    const noteList = [];
    if (!s.mode) {
        paintLines("", s);
        title.textContent = "Presets disponibles";
        aspect.setAttribute("hidden", "");
        desc.textContent = "Elige un modo de salida";
    }
    else {
        title.textContent =
            dcNow >= 3 && dcNow <= 5 && s.mode === "1080p x4" ? "1080p" : dcNow >= 3 && dcNow <= 5 && s.mode === "720p x3" ? "720p" : s.mode;
        // (after v107z) the Dreamcast in 480i: its 1080p has the two shapes too
        const dc43 = dcNow >= 3 && dcNow <= 5 && s.mode === "1080p x4";
        dcAspect1080 = dc43 || (dcNow === 5 && s.mode === "720p x3"); // (after v108f) the VGA's 720p switches live too
        // v107f: both shapes always in view, the one in use marked. Only 720p x3 can switch; every other mode is 16:9
        // only, so there 4:3 is shown switched off, so it reads as "not available here" rather than missing.
        aspect.removeAttribute("hidden");
        const shape = s.aspect || "16:9";
        // (after v108f) his names for the Dreamcast in 480i / 576i / VGA: «Normal» (the 2 x 2, internally 16:9) first and
        // «Panorámico» (the wider one, internally 4:3) for the games with a widescreen option; each with a small shape drawn
        // in it (a 4:3 box, a 16:9 box) so the two are told apart at a glance. Everywhere else, 4:3 and 16:9 as before.
        const dcNames = dcNow >= 3 && dcNow <= 5;
        nodelistToArray(aspect.querySelectorAll("[data-asp]")).forEach((b) => {
            const v = b.getAttribute("data-asp");
            v === shape ? b.setAttribute("active", "") : b.removeAttribute("active");
            const want = dcNames
                ? v === "16:9"
                    ? '<i class="gbs-asp__ic gbs-asp__ic--n"></i>Normal'
                    : '<i class="gbs-asp__ic gbs-asp__ic--w"></i>Panorámico'
                : v || "";
            if (b.innerHTML !== want)
                b.innerHTML = want;
            b.style.order = dcNames && v === "4:3" ? "2" : "";
            // v107h: a shape that does not exist in this mode is not shown at all (greyed out it only confused)
            // (after v107z) the Dreamcast in 480i / 576i: 720p x3 has only its 4:3 (the whole width)
            v === "4:3" && s.mode !== "720p x3" && !dc43 // (after v108e) every Dreamcast 720p has both shapes now
                ? b.setAttribute("hidden", "")
                : b.removeAttribute("hidden");
        });
        const d = (!s.snes && WORK_DESC_OTHER[s.key]) || WORK_DESC[s.key] || WORK_DESC[s.mode] || WORK_DESC[`${s.mode} 16:9`];
        // v107n: where the Líneas row is shown, it says how many lines are seen, so the first sentence of the mode goes
        const lines = paintLines(raw, s);
        // 720p x3 4:3: the picture already fills the screen at CRT, so steps 3 and 4 load the CRT shape again
        const wStep = s.key === "720p x3 4:3" ? Math.min(s.step, 1) : Math.min(s.step, 3);
        // Mega Drive: its games are 320 px wide, so the 320 step is whole pixels there, and that already is the shape a CRT
        // gave it. Its own sentence, separate from every other console.
        const w = s.step < 0 ? "" :
            s.md && wStep === 0 ? WORK_WIDTH_MD256 :
                s.md && wStep === 1 ? WORK_WIDTH_MD320 :
                    s.snes && wStep === 1 ? WORK_WIDTH_SNESCRT :
                        s.pce && wStep === 0 ? WORK_WIDTH_PCE256 :
                            s.pce && wStep === 1 ? WORK_WIDTH_PCECRT :
                                WORK_WIDTH[wStep];
        const dcd = dcModes ? DC_DESC[`${dcNow} ${s.mode}${s.aspect ? " " + s.aspect : ""}`] || DC_DESC[`${dcNow} ${s.mode}`] : "";
        desc.innerHTML = dcd
            ? dcd // (after v108f) no second line for the Dreamcast: the text and the button names already say it
            : d ? [lines.row ? "" : d[0], w, d[1]].filter((x) => !!x).join("<br>") : w;
        // v107f: 720p x5 at whole pixels is 256 x 5 = 1280 columns, the full width the scaler can draw in 720p
        if (s.mode === "720p x5" && s.step === 0) {
            noteList.push("Límite del lienzo: a 5 columnas por píxel la imagen ocupa todo el ancho que el escalador puede dibujar en 720p. " +
                "Los bordes negros de los lados están a propósito: tapan la basura que aparece pasado ese límite." +
                (s.snes ? " La pantalla de la Game Boy queda entera en el centro." : ""));
        }
        // (after v108c) the Dreamcast through VGA: its 2 x 2 lowers the ADC clock, and that clock can take the wifi down
        if (dcNow === 5 && s.mode === "1080p x4") {
            noteList.push("Aviso: el modo 16:9 (2 × 2) cambia el reloj de muestreo, y según el canal wifi del router puede dejar el GBS sin conexión. " +
                "Si pasa, apaga la consola y cambia el canal del router (con el 11 funciona). El 4:3 no tiene este problema.");
        }
        if (WORK_CROPS[s.key]) {
            // v107f: one wording for every mode that leaves lines out, with its own factor
            const f = s.mode.slice(-2); // "x4" / "x5"
            noteList.push(`Al multiplicar ${f} algunas líneas quedan fuera de la pantalla. Mueve la imagen para centrarla a tu gusto.`);
        }
    }
    notes.innerHTML = noteList
        .map((n) => `<div class="gbs-note"><span class="gbs-note__ico">&#9650;</span><span>${n}</span></div>`)
        .join("");
    // v107j: the memory buttons say what the memory is set to, read from the fourth line of the status bar
    const mem = /^mem (\d+) (\d+)(?: sl (\d+))?$/m.exec(raw);
    const memClock = document.getElementById("memClock");
    const memPhase = document.getElementById("memPhase");
    if (memClock)
        memClock.textContent = mem ? (mem[1] === "0" ? "stock" : `${mem[1]} MHz`) : "—"; // (after v107v) his word: "stock" (nobody would understand "realimentado")
    if (memPhase)
        memPhase.textContent = mem ? `${mem[2]} de 15` : "—";
    // v107j: every other picture control, from the fifth line ("v pk=1 st=1 ...")
    paintPictureValues(raw);
    // v107j: the scanline intensity, on the same data line
    const slVal = document.getElementById("slVal");
    if (slVal)
        slVal.textContent = mem && mem[3] !== undefined ? `${mem[3]} de 10` : "—";
    const fineBtn = document.getElementById("workFine");
    if (fineBtn)
        s.fine ? fineBtn.setAttribute("active", "") : fineBtn.removeAttribute("active");
    // v107j: the console choice in Modo. "profile auto SNES" = Auto is the choice and SNES is what it picked
    const cons = document.getElementById("workConsole");
    if (cons) {
        const pm = /profile (auto )?(MD|SNES|PC-Engine)/.exec(raw);
        const bp = barProfile(raw);
        // (after v107w) Auto can now pick one of them too (the Dreamcast): then Auto is the choice, and the text is theirs
        const emptyInUse = !!bp && UNMEASURED.indexOf(bp.name) >= 0;
        const empty = emptyInUse && !bp.auto;
        // (after v108e) whatever profile the bar names by hand (the Dreamcast now too) is the one lit
        const choice = bp ? (bp.auto ? "auto" : bp.name) : pm ? (pm[1] ? "auto" : pm[2]) : "";
        // the Consola card and the unmeasured ones below it
        nodelistToArray(document.querySelectorAll("#workConsole [data-prof], #workConsoleOthers [data-prof]")).forEach((b) => b.getAttribute("data-prof") === choice ? b.setAttribute("active", "") : b.removeAttribute("active"));
        // (after v107v) an unmeasured profile in use: the list stays open and the text under Consola says what it is for
        if (empty)
            consoleOthersShow(true);
        const consDesc = document.getElementById("workConsoleDesc");
        if (consDesc)
            consDesc.textContent = bp && bp.name === "Others" ? CONSOLE_DESC_OTHERS : emptyInUse ? CONSOLE_DESC_EMPTY : CONSOLE_DESC;
        const autoBtn = cons.querySelector('[data-prof="auto"]');
        const names = { MD: "Mega Drive", SNES: "SNES", "PC-Engine": "PC Engine" };
        if (autoBtn)
            autoBtn.textContent = bp && bp.auto ? `Auto · ${names[bp.name] || bp.name}` : "Auto";
    }
    // v107h: the colour tab says whose colour profile it is saving
    // v107p: from "cfg=" (whose picture memory is in use), which knows an unknown console from the Mega Drive
    // (after v107q) and its replica at the end of Filtros, so every label with the class, not only the one id
    const cfgM = /\bcfg=(\d+)/.exec(raw); // (after v107v) up to 17
    settingsName = cfgM ? SETTINGS_NAMES[parseInt(cfgM[1], 10)] || "" : "";
    nodelistToArray(document.querySelectorAll(".gbs-colprof-label")).forEach((l) => (l.textContent = settingsName ? `Perfil de color y filtro · ${settingsName}` : "Perfil de color y filtro"));
    // v107h: the Ajuste tab names what it is adjusting
    const placeLabel = document.getElementById("workPlaceLabel");
    if (placeLabel) {
        const stepNames = s.md ? ["256 px", "320 px", "ancho", "llenar"] : ["256 px", "CRT", "ancho", "llenar"];
        const st = s.key === "720p x3 4:3" || s.steps === 2 ? Math.min(s.step, 1) : s.step;
        placeLabel.textContent = s.mode
            ? `Colocación · ${s.mode}${st >= 0 ? " " + stepNames[Math.min(st, 3)] : ""}`
            : "Colocación · preset de fábrica";
    }
    // v107f: the size buttons say what they are for on each console. SNES: every game is 256 px, so the first is named by
    // that width and the second is the CRT shape. Mega Drive: 256 and 320 are both whole pixels, each for its own games.
    // Anything else keeps the generic names.
    const chipText = s.md
        ? [["256 px", "píxel exacto"], ["320 px", "píxel exacto"], ["ancho", "experimental"], ["llenar", "experimental"]]
        : s.snes || s.pce
            ? [["256 px", "píxel exacto"], ["CRT", "aspecto de TV"], ["ancho", "experimental"], ["llenar", "experimental"]]
            : [["cuadrado", "256 px, píxel exacto"], ["CRT", "aspecto de TV"], ["ancho", "experimental"], ["llenar", "experimental"]];
    nodelistToArray(document.querySelectorAll("#workSteps [data-step]")).forEach((c) => {
        const t = chipText[parseInt(c.getAttribute("data-step") || "0", 10)];
        const big = c.querySelector("span");
        const small = c.querySelector("small");
        if (t && big)
            big.textContent = t[0];
        if (t && small)
            small.textContent = t[1];
    });
    // v107m: one button per size step (256 / CRT / ancho / llenar). Until now the third button stood for both wide steps
    // and lit on the first of them, so it looked finished when one more press still gave the full width.
    // v107f: 720p x3 4:3 and 720p x5 have two sizes only, so ancho and llenar go away there, and a step 3 or 4 left over
    // from another mode lights CRT, which is what is loaded.
    // NTSC has no size steps yet (no "step n/m" in the bar), only the old 256 / 320 / wide: there ancho is hidden and
    // llenar does what the single button did.
    // (after v107q) with no whole-pixel mode on (stock preset, nothing connected) there is no size to choose: the row goes
    const stepsRow = document.getElementById("workSteps");
    // (after v107z) the Dreamcast's modes have no sizes: each one is the measured picture for its signal
    if (stepsRow)
        s.mode && !dcModes ? stepsRow.removeAttribute("hidden") : stepsRow.setAttribute("hidden", "");
    // (after v107r) the edge note, his text, in every whole-pixel mode (the stock preset has no edge to keep), and not in
    // DOLOR: a joke, nothing is kept there (his decision)
    const edgeNote = document.getElementById("workEdgeNote");
    const lnDolor = /\bln=2\b/.test(raw) && /\blna=\d*[13579]\b/.test(raw);
    if (edgeNote)
        s.mode && !lnDolor ? edgeNote.removeAttribute("hidden") : edgeNote.setAttribute("hidden", "");
    const twoSizes = s.key === "720p x3 4:3" || s.steps === 2; // v107f: the firmware says "step n/2" where only two exist
    const oldFill = !twoSizes && s.steps === 0;
    nodelistToArray(document.querySelectorAll("#workSteps [data-step]")).forEach((c) => {
        const n = parseInt(c.getAttribute("data-step") || "0", 10);
        // v107q: three sizes (the Mega Drive in PAL 1080p x4, "step n/3"): no llenar
        const hide = (n >= 2 && twoSizes) || (n === 2 && oldFill) || (n === 3 && s.steps === 3);
        hide ? c.setAttribute("hidden", "") : c.removeAttribute("hidden");
        const st = twoSizes ? Math.min(s.step, 1) : s.steps === 3 ? Math.min(s.step, 2) : s.step;
        const on = !!s.mode && (oldFill && n === 3 ? st >= 2 : st === n);
        on ? c.setAttribute("active", "") : c.removeAttribute("active");
    });
    nodelistToArray(document.querySelectorAll("#workModes [data-mode]")).forEach((c) => c.getAttribute("data-mode") === s.mode ? c.setAttribute("active", "") : c.removeAttribute("active"));
};
// v107j: the values of the Color and Filtros controls. Each one goes up when its + is pressed (the firmware sends the
// ADC gain inverted for that reason), so the number always moves the way the button says.
const PEAK_NAMES = ["suave", "original", "más", "bastante más", "máximo"];
const STEP_NAMES = ["sin efecto", "original", "más", "máximo"];
const NOISE_NAMES = ["original", "ligero (110 MHz)", "medio (70 MHz)", "fuerte (40 MHz)"];
const paintPictureValues = (raw) => {
    const line = /^v (.*)$/m.exec(raw);
    const v = {};
    if (line) {
        line[1].split(" ").forEach((kv) => {
            const m = /^(\w+)=(-?\d+)$/.exec(kv);
            if (m)
                v[m[1]] = parseInt(m[2], 10);
        });
    }
    const has = (k) => v[k] !== undefined;
    const fmt = {
        // the brightness register is 8 bits and "brightness -" from 0 wraps to 255: shown signed, so - from 0 reads -1
        br: () => `${v.br > 127 ? v.br - 256 : v.br}`,
        ct: () => `${v.ct}`,
        bl: () => (v.bl < 0 ? "apagado (lo del preset)" : `${v.bl}`),
        ga: () => `${v.ga}${v.gauto ? " · automática" : ""}`,
        sa: () => (v.su === v.sv ? `${v.su}` : `${v.su} / ${v.sv}`),
        su: () => `${v.su}`,
        sv: () => `${v.sv}`,
        hu: () => `${v.hu > 0 ? "+" : ""}${v.hu}°`,
        wr: () => `${v.wr}`,
        wg: () => `${v.wg}`,
        wb: () => `${v.wb}`,
        tu: () => `${v.tu}`,
        tv: () => `${v.tv}`,
        oall: () => `${v.or} · ${v.og} · ${v.ob}`,
        or: () => `${v.or}`,
        og: () => `${v.og}`,
        ob: () => `${v.ob}`,
        pf: () => `${v.pf}`, // the preset value is in the note right below
        sg: () => `${v.sg} de 15${has("stb") ? (v.sg === v.stb ? " · el del preset" : ` · preset ${v.stb}`) : ""}`,
        ph: () => `${v.ph} de 31${v.pa ? " · automática" : ""}`,
        pk: () => `ahora: ${PEAK_NAMES[v.pk] || v.pk}`,
        // v107l: the quick window controls of Ajuste, shown with the register numbers the timing dumps use
        el: () => `${v.dhp}`, // left edge = VDS_DIS_HB_SP
        er: () => `${v.dhs}`, // right edge = VDS_DIS_HB_ST
        et: () => `${v.dvp}`, // top edge = VDS_DIS_VB_SP
        eb: () => `${v.dvs}`, // bottom edge = VDS_DIS_VB_ST
        hb: () => `${v.hbs} · ${v.hbp}`,
        hbin: () => `${v.hbin}`,
        vb: () => `${v.vbs} · ${v.vbp}`, // v107n: the vertical memory window, VDS_VB_ST / SP
        st: () => `ahora: ${STEP_NAMES[v.st] || v.st}`,
        nf: () => `ahora: ${NOISE_NAMES[v.nf] || v.nf}`,
    };
    const need = { sa: ["su", "sv"], oall: ["or", "og", "ob"], ga: ["ga"], ph: ["ph"], el: ["dhp"], er: ["dhs"], et: ["dvp"], eb: ["dvs"], hb: ["hbs", "hbp"], vb: ["vbs", "vbp"] };
    // the peaking note names the preset's own value, so there is a reference to go back to
    const pkNote = document.getElementById("pkNote");
    if (pkNote) {
        pkNote.textContent =
            "Al aumentar la nitidez de forma artificial generamos halos blancos y el ruido será más visible. " +
                "Señales borrosas a veces se benefician, aunque dependerá de tu gusto. " +
                "Prueba y decide.";
    }
    nodelistToArray(document.querySelectorAll("[data-val]")).forEach((el) => {
        const k = el.getAttribute("data-val");
        const keys = need[k] || [k];
        const ok = !!fmt[k] && keys.every(has);
        const target = el.classList.contains("gbs-chip") ? el.querySelector("small") : el.querySelector(".gbs-step__val");
        if (target)
            target.textContent = ok ? fmt[k]() : "—";
    });
};
const openSection = (name) => {
    const b = document.querySelector(`.gbs-menu [gbs-section="${name}"]`);
    if (b)
        b.click();
};
const initWorkScreen = () => {
    // v107h: the pad lives in its own tab, "Ajuste", instead of a bar fixed over every screen
    const bar = document.getElementById("padKeys");
    const seg = document.getElementById("padSeg");
    const mid = document.getElementById("padMid");
    const help = document.getElementById("padHelp");
    if (!bar || !seg || !mid) {
        return;
    }
    let mode = "move";
    // (after v107s) his design: in Bordes each key is two arrows, one per direction, so the pad never has to be switched
    // between "open" and "close" while it is used. Each arrow pushes that edge the way it points; the outward one (green)
    // opens the edge (less black), the inward one closes it. (after v107t) all arrows white and no explanation: he found it odd.
    const ARROW = { up: "&#9650;", down: "&#9660;", left: "&#9664;", right: "&#9654;" };
    const DOUBLE = {
        // [first arrow, second arrow] as drawn, each "dir:out" or "dir:in"
        up: ["out", "in"], // ▲ opens the top edge, ▼ closes it
        down: ["in", "out"], // ▲ closes the bottom edge, ▼ opens it
        left: ["out", "in"], // ◀ opens the left edge, ▶ closes it
        right: ["in", "out"], // ◀ closes the right edge, ▶ opens it
    };
    const DOUBLE_ARROWS = {
        up: ["&#9650;", "&#9660;"], down: ["&#9650;", "&#9660;"], left: ["&#9664;", "&#9654;"], right: ["&#9664;", "&#9654;"],
    };
    const drawKeys = () => {
        nodelistToArray(bar.querySelectorAll("[data-pad]")).forEach((key) => {
            const dir = key.getAttribute("data-pad");
            if (mode === "crop") {
                key.classList.add("gbs-pad__k--dbl");
                (dir === "up" || dir === "down") ? key.classList.add("gbs-pad__k--v") : key.classList.remove("gbs-pad__k--v");
                const [a, b] = DOUBLE[dir];
                const [ca, cb] = DOUBLE_ARROWS[dir];
                key.innerHTML = `<span data-act="${a}">${ca}</span><span data-act="${b}">${cb}</span>`; // all white (after v107t)
            }
            else {
                key.classList.remove("gbs-pad__k--dbl", "gbs-pad__k--v");
                key.innerHTML = ARROW[dir];
            }
        });
    };
    const paint = () => {
        nodelistToArray(seg.querySelectorAll("[data-mode]")).forEach((b) => b.getAttribute("data-mode") === mode ? b.setAttribute("active", "") : b.removeAttribute("active"));
        drawKeys();
        mid.textContent = mode === "move" ? "MOVER" : mode === "crop" ? "BORDES" : "ESCALA";
        if (help) {
            help.textContent =
                mode === "move" ? "▲▼ sube o baja la imagen · ◀▶ la desplaza a los lados" :
                    mode === "crop" ? "Cada flecha mueve su borde hacia donde apunta" : // (after v107t) short, like the other two
                        "▲▼ más o menos alta · ◀▶ más o menos ancha";
        }
    };
    nodelistToArray(seg.querySelectorAll("[data-mode]")).forEach((b) => b.addEventListener("click", () => {
        mode = b.getAttribute("data-mode");
        paint();
    }));
    nodelistToArray(bar.querySelectorAll("[data-pad]")).forEach((key) => {
        const dir = key.getAttribute("data-pad");
        key.addEventListener("click", (ev) => {
            if (mode === "crop") {
                // which of the two arrows: a tap on the gap between them does nothing
                const half = ev.target.closest("[data-act]");
                if (!half)
                    return;
                loadUser(half.getAttribute("data-act") === "out" ? PAD_CROP_OUT[dir] : PAD_KEYS.crop[dir].msg);
            }
            else {
                const k = PAD_KEYS[mode][dir];
                (k.type === "user" ? loadUser : loadDoc)(k.msg);
            }
            if (mode === "move") {
                if (dir === "left")
                    work.moveX--;
                if (dir === "right")
                    work.moveX++;
                if (dir === "up")
                    work.moveY--;
                if (dir === "down")
                    work.moveY++;
            }
            else if (mode === "crop") {
                work.crop++;
            }
            else {
                work.scale++;
            }
            work.dirty = true;
            paintWork();
        });
    });
    // (after v107r) the three undo buttons; the edge one asks first and clears its counter in the "gbs-click" handler
    const undoMove = document.getElementById("workUndoMove");
    if (undoMove)
        undoMove.addEventListener("click", () => resetWorkPart("move"));
    const undoScale = document.getElementById("workUndoScale");
    if (undoScale)
        undoScale.addEventListener("click", () => resetWorkPart("scale"));
    const save = () => {
        const slot = document.querySelector('[gbs-role="slot"][active]');
        if (!slot) {
            gbsAlert("Elige primero una ranura en Presets personales, al final de esta pestaña, y vuelve a pulsar guardar.").then(() => {
                const card = document.getElementById("workSlot");
                const scroller = document.querySelector(".gbs-scroll");
                if (card && scroller) {
                    scroller.scrollTo({ top: card.getBoundingClientRect().top - scroller.getBoundingClientRect().top + scroller.scrollTop - 160, behavior: "smooth" });
                }
            });
            return;
        }
        savePreset();
        work.dirty = false;
        const saved = document.getElementById("workSaved");
        if (saved) {
            saved.setAttribute("ok", "");
            saved.textContent = "guardado";
        }
        paintWork();
    };
    // v107h: the fine step, at hand while adjusting. Its real state comes back in the status bar; the button flips at
    // once so the tap is seen, and the next status bar corrects it if the firmware disagrees.
    const fineBtn = document.getElementById("workFine");
    if (fineBtn) {
        fineBtn.addEventListener("click", () => {
            loadUser("8");
            fineBtn.hasAttribute("active") ? fineBtn.removeAttribute("active") : fineBtn.setAttribute("active", "");
        });
    }
    // the reminder takes you to where the profile is saved
    const remind = document.getElementById("workRemind");
    if (remind) {
        remind.addEventListener("click", () => {
            openSection("imagen");
            // scroll only the content area: scrollIntoView also moves the outer page and drags the tab bar up with it
            const card = document.getElementById("workSlot");
            const scroller = document.querySelector(".gbs-scroll");
            if (card && scroller) {
                setTimeout(() => {
                    const y = card.getBoundingClientRect().top - scroller.getBoundingClientRect().top + scroller.scrollTop - 160;
                    scroller.scrollTo({ top: Math.max(0, y), behavior: "smooth" });
                }, 50);
            }
        });
    }
    const slotSave = document.getElementById("workSlotSave");
    if (slotSave)
        slotSave.addEventListener("click", save);
    // 720p x3: the two shapes. Tapping the one already in use does nothing.
    const aspect = document.getElementById("workAspect");
    if (aspect) {
        nodelistToArray(aspect.querySelectorAll("[data-asp]")).forEach((b) => b.addEventListener("click", () => {
            if (b.hasAttribute("disabled") || b.hasAttribute("active"))
                return;
            // (after v107z) the Dreamcast's 1080p in 480i switches with its own code (74)
            if (dcAspect1080)
                loadImg("74");
            else
                b.getAttribute("data-asp") === "4:3" ? loadUser("^") : loadImg("10");
        }));
    }
    paint();
    paintWork();
    initTabs();
    const active = document.querySelector(".gbs-menu button[active]");
    updateWorkBar(active ? active.getAttribute("gbs-section") || "imagen" : "imagen");
};
// v107e: three tabs instead of seven icons. The seven sections are all still there; the tabs group them, and a row of
// small tabs at the top picks the section inside a group. The swipe still walks every section in order.
// v107h: five tabs, no sub-tabs in Sistema. The section names are the old internal ones ("imagen" is the Modo tab).
const TAB_GROUPS = {
    modo: [{ section: "imagen", label: "Modo" }],
    ajuste: [{ section: "ajuste", label: "Ajuste" }],
    imagen: [
        { section: "color", label: "Color" },
        { section: "filters", label: "Filtros" },
    ],
    // v107l: one page, no sub-tabs: VGA, sync, the old position tools and (with developer mode) the developer tools, in
    // that order. The developer block is a div carrying gbs-section="developer", which updateDeveloperMode() shows / hides.
    avanzado: [{ section: "presets", label: "Avanzado" }],
    sistema: [{ section: "system", label: "Sistema" }],
};
const lastInGroup = {};
const groupOf = (section) => Object.keys(TAB_GROUPS).find((g) => TAB_GROUPS[g].some((e) => e.section === section)) || "modo";
const sectionAvailable = (section) => {
    const b = document.querySelector(`.gbs-menu [gbs-section="${section}"]`);
    return !!b && !b.hasAttribute("hidden");
};
const initTabs = () => {
    const tabs = document.getElementById("gbsTabs");
    if (!tabs)
        return;
    nodelistToArray(tabs.querySelectorAll("[data-group]")).forEach((t) => t.addEventListener("click", () => {
        const g = t.getAttribute("data-group");
        const list = TAB_GROUPS[g].filter((e) => sectionAvailable(e.section));
        const target = lastInGroup[g] && sectionAvailable(lastInGroup[g]) ? lastInGroup[g] : list[0].section;
        openSection(target);
    }));
};
const paintTabs = (section) => {
    const g = groupOf(section);
    lastInGroup[g] = section;
    const tabs = document.getElementById("gbsTabs");
    if (tabs) {
        nodelistToArray(tabs.querySelectorAll("[data-group]")).forEach((t) => t.getAttribute("data-group") === g ? t.setAttribute("active", "") : t.removeAttribute("active"));
    }
    const sub = document.getElementById("gbsSubtabs");
    if (!sub)
        return;
    const list = TAB_GROUPS[g].filter((e) => sectionAvailable(e.section));
    if (list.length < 2) {
        sub.setAttribute("hidden", "");
        return;
    }
    sub.removeAttribute("hidden");
    sub.innerHTML = list
        .map((e) => `<button data-sec="${e.section}"${e.section === section ? " active" : ""}>${e.label}</button>`)
        .join("");
    nodelistToArray(sub.querySelectorAll("[data-sec]")).forEach((b) => b.addEventListener("click", () => openSection(b.getAttribute("data-sec"))));
};
// the bar belongs to the working screen only; the tabs follow whichever section is shown
const updateWorkBar = (section) => {
    paintTabs(section);
};
/** inits */
// v107b: the first piece of the new interface. The sections can now be swiped through as well as tapped, because this
// thing is used with the eyes on the TV and not on the phone: a swipe needs no aim, a tap on a 60-pixel menu entry does.
// Tapping keeps working exactly as before; nothing else about the sections changes.
const initMenuButtons = () => {
    const menuButtons = nodelistToArray(document.querySelector(".gbs-menu").querySelectorAll("button"));
    const sections = nodelistToArray(document.querySelectorAll("section"));
    const scroll = document.querySelector(".gbs-scroll");
    // the order the menu shows, which is the order a swipe walks through
    const order = menuButtons
        .map((b) => b.getAttribute("gbs-section"))
        .filter((n) => !!n);
    const showSection = (name, animate) => {
        const target = document.querySelector(`section[name="${name}"]`);
        if (!target) {
            return;
        }
        sections.forEach((s) => s.setAttribute("hidden", ""));
        target.removeAttribute("hidden");
        menuButtons.forEach((btn) => btn.getAttribute("gbs-section") === name
            ? btn.setAttribute("active", "")
            : btn.removeAttribute("active"));
        scroll.scrollTo(0, 1);
        updateWorkBar(name);
        if (animate) {
            // a short slide so the eye catches which way it moved, even seen out of the corner
            target.style.animation = "none";
            void target.offsetWidth; // restart it
            target.style.animation = `gbs-slide-${animate} 160ms ease-out`;
        }
    };
    const currentIndex = () => {
        const active = menuButtons.find((b) => b.hasAttribute("active"));
        const i = active ? order.indexOf(active.getAttribute("gbs-section")) : 0;
        return i < 0 ? 0 : i;
    };
    const step = (dir) => {
        const next = currentIndex() + dir;
        if (next < 0 || next >= order.length) {
            return; // no wrap-around: the ends are a useful landmark when you are not looking
        }
        showSection(order[next], dir === 1 ? "left" : "right");
    };
    menuButtons.forEach((button) => button.addEventListener("click", () => showSection(button.getAttribute("gbs-section"))));
    // --- the swipe itself
    let x0 = 0;
    let y0 = 0;
    let tracking = false;
    let decided = null;
    scroll.addEventListener("touchstart", (e) => {
        if (e.touches.length !== 1) {
            tracking = false;
            return;
        }
        // a drag that starts inside the log must scroll the log, not change section
        const from = e.target;
        if (from && from.closest(".gbs-output__textarea, input, select, textarea")) {
            tracking = false;
            return;
        }
        x0 = e.touches[0].clientX;
        y0 = e.touches[0].clientY;
        tracking = true;
        decided = null;
    }, { passive: true });
    scroll.addEventListener("touchmove", (e) => {
        if (!tracking || e.touches.length !== 1) {
            return;
        }
        const dx = e.touches[0].clientX - x0;
        const dy = e.touches[0].clientY - y0;
        if (decided === null) {
            // lock to one axis early, and only call it horizontal when it clearly is: the list below scrolls
            // vertically and a diagonal thumb must not steal that
            if (Math.abs(dx) < 12 && Math.abs(dy) < 12) {
                return;
            }
            decided = Math.abs(dx) > Math.abs(dy) * 1.7 ? "x" : "y";
        }
        if (decided === "x" && e.cancelable) {
            e.preventDefault(); // stop the page rubber-banding sideways while swiping
        }
    }, { passive: false });
    const finish = (e) => {
        if (!tracking || decided !== "x") {
            tracking = false;
            return;
        }
        tracking = false;
        const t = e.changedTouches[0];
        const dx = t.clientX - x0;
        if (Math.abs(dx) >= 60) {
            step(dx < 0 ? 1 : -1); // dragging left brings the next section in from the right
        }
    };
    scroll.addEventListener("touchend", finish);
    scroll.addEventListener("touchcancel", () => {
        tracking = false;
    });
};
const initGBSButtons = () => {
    const actions = {
        user: loadUser,
        img: loadImg,
        action: loadDoc,
        setSlot,
    };
    const buttons = nodelistToArray(document.querySelectorAll("[gbs-click]"));
    buttons.forEach((button) => {
        const clickMode = button.getAttribute("gbs-click");
        const message = button.getAttribute("gbs-message");
        const messageType = button.getAttribute("gbs-message-type");
        const action = actions[messageType];
        if (clickMode === "normal") {
            const confirmText = button.getAttribute("gbs-confirm"); // v73
            button.addEventListener("click", () => {
                // (after v107r) with a personal profile loaded "deshacer borde" goes back to the profile's edge and erases
                // nothing, so it does not ask
                if (button.id === "workUndoEdge" && customPresetOn) {
                    action(message);
                    resetWorkPart("edge");
                    return;
                }
                if (confirmText) {
                    gbsConfirm(confirmText).then(() => {
                        action(message);
                        if (button.id === "workUndoEdge")
                            resetWorkPart("edge"); // (after v107r) only once accepted
                    }, () => { });
                }
                else {
                    action(message);
                }
            });
        }
        if (clickMode === "repeat") {
            const callback = () => {
                action(message);
            };
            bindPressRepeat(button, callback);
        }
    });
};
/** v40: "Copy diagnostics" button: asks for the video timings, then copies status + timings + log */
const copyTextToClipboard = (text) => {
    const ta = document.createElement("textarea");
    ta.value = text;
    ta.setAttribute("readonly", "");
    ta.style.position = "fixed";
    ta.style.top = "0";
    ta.style.left = "0";
    ta.style.opacity = "0";
    document.body.appendChild(ta);
    ta.focus();
    ta.select();
    try {
        ta.setSelectionRange(0, text.length);
    }
    catch (e) { }
    let ok = false;
    try {
        ok = document.execCommand("copy");
    }
    catch (e) {
        ok = false;
    }
    document.body.removeChild(ta);
    return ok;
};
const buildDiagnosticsText = () => {
    const statusBar = document.getElementById("gbsStatusBar");
    const log = GBSControl.ui.terminal.value;
    const tail = log.length > 6000 ? log.substring(log.length - 6000) : log;
    return [
        "GBS-Control diagnostics (web UI 3x4x-v108h)",
        new Date().toISOString(),
        "Status: " + (statusBar ? statusBar.textContent : "n/a"),
        "----- log (video timings first requested) -----",
        tail,
    ].join("\n");
};
const finishDiagnostics = (button, label) => {
    const text = buildDiagnosticsText();
    let message = "";
    if (copyTextToClipboard(text)) {
        message = "Copied";
    }
    else {
        // the browser blocked the copy: hand the same text over as a file instead
        downloadBlob(new Blob([text], { type: "text/plain" }), "gbs-diagnostics-" + +new Date() + ".txt");
        message = "Saved as file";
    }
    button.textContent = message;
    setTimeout(() => {
        button.textContent = label;
        button.removeAttribute("disabled");
    }, 2000);
};
const initDiagnosticsButton = () => {
    document.querySelectorAll("[gbs-diag-copy]").forEach((btn) => {
        const label = btn.textContent;
        // v107o: an /img code in the attribute asks for that instead of the video timings ("54" = the console fingerprint)
        const code = btn.getAttribute("gbs-diag-copy");
        btn.addEventListener("click", () => {
            btn.setAttribute("disabled", "");
            btn.textContent = "Reading...";
            // (after v107y) his report: the copy sometimes came out without the answer (in 1080p x5 the board and its Wi-Fi are
            // slower). Instead of one fixed 1.5 s wait, it now waits for the answer itself ("HT / scale" for the timings,
            // "huella:" for the fingerprint), checking every 250 ms, and copies as soon as it is there or after 4 s at most
            // (the browser still counts that as part of the click; if it does not, the text is saved as a file).
            const term = GBSControl.ui.terminal;
            const before = term.value.length + GBSControl.queuedText.length;
            const marker = code ? "huella:" : "HT / scale";
            (code ? loadImg(code) : loadDoc(",")).catch(() => { });
            const started = Date.now();
            const poll = () => {
                if (GBSControl.queuedText.length > 0) {
                    term.value += GBSControl.queuedText;
                    GBSControl.queuedText = "";
                }
                const arrived = term.value.indexOf(marker, Math.max(0, before - 200)) >= 0;
                if ((arrived && Date.now() - started >= 500) || Date.now() - started >= 4000) {
                    finishDiagnostics(btn, label);
                }
                else {
                    setTimeout(poll, 250);
                }
            };
            setTimeout(poll, 250);
        });
    });
};
/** v41: share profiles. Export one slot to a .gbsp file, import a .gbsp file into an empty slot. */
const PROFILE_KINDS = [
    "ntsc",
    "pal",
    "ntsc_480p",
    "pal_576p",
    "ntsc_720p",
    "ntsc_1080p",
    "medium_res",
    "vga_upscale",
    "unknown",
];
const PROFILE_SLOT_CHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~()!*:,".split("");
const PROFILE_VALUES = 432; // registers stored in one preset file
const PROFILE_SLOT_BYTES = 32; // one entry of slots.bin
const profileLog = (text) => {
    GBSControl.ui.terminal.value += "\n[profile] " + text + "\n";
    GBSControl.ui.terminal.scrollTop = GBSControl.ui.terminal.scrollHeight;
};
const profileNotify = (text) => {
    profileLog(text);
    return gbsAlert(text).catch(() => { });
};
const selectedSlotIndex = () => {
    const el = document.querySelector('[gbs-role="slot"][active]');
    return el ? parseInt(el.getAttribute("gbs-slot-id"), 10) : -1;
};
// preset file text ("12,\r\n34,\r\n ... };") -> array of 432 numbers (or null)
const parsePresetText = (text) => {
    const body = String(text).split("}")[0];
    const values = body
        .split(",")
        .map((s) => s.trim())
        .filter((s) => s !== "")
        .map((s) => (/^[0-9]{1,3}$/.test(s) ? parseInt(s, 10) : -1));
    if (values.length !== PROFILE_VALUES || values.some((v) => v < 0 || v > 255)) {
        return null;
    }
    return values;
};
const exportProfile = () => {
    const idx = selectedSlotIndex();
    if (idx < 0) {
        profileNotify("Select the slot that holds the profile first, then press export.");
        return;
    }
    const chr = PROFILE_SLOT_CHARS[idx];
    let meta = null;
    fetchSlotNames()
        .then((ok) => {
        if (!ok) {
            throw new Error("Could not read the slot list.");
        }
        meta = GBSControl.structs.slots[idx];
        if (!meta || meta.name === "Empty" || meta.name === "") {
            throw new Error("This slot is empty: save a preset into it first.");
        }
        return fetch("/filesystem/dir?" + +new Date()).then((r) => r.json());
    })
        .then((dir) => {
        const kinds = PROFILE_KINDS.filter((kind) => dir.indexOf("/preset_" + kind + "." + chr) >= 0);
        if (kinds.length === 0) {
            throw new Error("No preset files found for this slot.");
        }
        const files = {};
        const funcs = kinds.map((kind) => () => {
            const path = "/preset_" + kind + "." + chr;
            return fetch("/filesystem/download?file=" + encodeURIComponent(path) + "&" + +new Date())
                .then((response) => checkFetchResponseStatus(response) && response.text())
                .then((text) => {
                const values = parsePresetText(text);
                if (!values) {
                    throw new Error("Unexpected format in " + path);
                }
                files[kind] = values;
            });
        });
        return serial(funcs).then(() => files);
    })
        .then((files) => {
        const profile = {
            gbsProfile: 1,
            firmware: "3x4x-v108h",
            name: meta.name,
            meta: {
                presetID: meta.presetID,
                scanlines: meta.scanlines,
                scanlinesStrength: meta.scanlinesStrength,
                wantVdsLineFilter: meta.wantVdsLineFilter,
                wantStepResponse: meta.wantStepResponse,
                wantPeaking: meta.wantPeaking,
            },
            files,
        };
        const safeName = meta.name.replace(/[^A-Za-z0-9._-]+/g, "_") || "profile";
        downloadBlob(new Blob([JSON.stringify(profile)], { type: "application/json" }), "gbs-profile-" + safeName + ".gbsp");
        profileNotify("Profile '" + meta.name + "' exported (" + Object.keys(files).length + " resolution(s)).");
    })
        .catch((e) => {
        profileNotify("Export failed: " + (e && e.message ? e.message : "unknown error"));
    });
};
// returns "" when the profile is valid, otherwise the reason
const validateProfile = (p) => {
    if (!p || p.gbsProfile !== 1 || !p.files || typeof p.files !== "object" || !p.meta) {
        return "This is not a GBS profile file.";
    }
    if (typeof p.name !== "string" || p.name.length < 1 || p.name.length > 24 || !/^[\x20-\x7e]+$/.test(p.name)) {
        return "The profile name is not valid.";
    }
    const metaKeys = ["presetID", "scanlines", "scanlinesStrength", "wantVdsLineFilter", "wantStepResponse", "wantPeaking"];
    for (let i = 0; i < metaKeys.length; i++) {
        const v = p.meta[metaKeys[i]];
        if (!Number.isInteger(v) || v < 0 || v > 255) {
            return "The profile settings are not valid.";
        }
    }
    const kinds = Object.keys(p.files);
    if (kinds.length === 0) {
        return "The profile has no presets.";
    }
    for (let i = 0; i < kinds.length; i++) {
        const values = p.files[kinds[i]];
        if (PROFILE_KINDS.indexOf(kinds[i]) < 0 ||
            !Array.isArray(values) ||
            values.length !== PROFILE_VALUES ||
            values.some((v) => !Number.isInteger(v) || v < 0 || v > 255)) {
            return "The preset data is not valid (" + kinds[i] + ").";
        }
    }
    return "";
};
const uploadProfileFile = (fileName, content) => {
    const formData = new FormData();
    formData.append("file", new Blob([content], { type: "application/octet-stream" }), fileName);
    return fetch("/filesystem/upload", { method: "POST", body: formData }).then((response) => checkFetchResponseStatus(response));
};
const importProfileText = (text) => {
    let profile = null;
    try {
        profile = JSON.parse(text);
    }
    catch (e) {
        profile = null;
    }
    const problem = validateProfile(profile);
    if (problem) {
        profileNotify("Import failed: " + problem);
        return;
    }
    let target = -1;
    fetchSlotNames()
        .then((ok) => {
        if (!ok) {
            throw new Error("Could not read the slot list.");
        }
        const slots = GBSControl.structs.slots;
        const isEmpty = (i) => slots[i].name === "Empty" || slots[i].name === "";
        const selected = selectedSlotIndex();
        if (selected >= 0 && isEmpty(selected)) {
            target = selected;
        }
        else {
            for (let i = 0; i < slots.length; i++) {
                if (isEmpty(i)) {
                    target = i;
                    break;
                }
            }
        }
        if (target < 0) {
            throw new Error("No empty slot left. Remove a preset first.");
        }
        const chr = PROFILE_SLOT_CHARS[target];
        const kinds = Object.keys(profile.files);
        // preset files first, the slot entry last: an interrupted import leaves the slot empty
        const funcs = kinds.map((kind) => () => {
            const content = profile.files[kind].map((v) => v + ",\r\n").join("") + "};\r\n";
            return uploadProfileFile("preset_" + kind + "." + chr, content);
        });
        return serial(funcs);
    })
        .then(() => fetch("/bin/slots.bin?" + +new Date()))
        .then((response) => response.arrayBuffer())
        .then((arrayBuffer) => {
        if (arrayBuffer.byteLength !== PROFILE_SLOT_BYTES * GBSControl.maxSlots) {
            throw new Error("Unexpected slot list size.");
        }
        const bytes = new Uint8Array(arrayBuffer);
        const base = target * PROFILE_SLOT_BYTES;
        for (let i = 0; i < 24; i++) {
            bytes[base + i] = i < profile.name.length ? profile.name.charCodeAt(i) : 0x20;
        }
        bytes[base + 24] = 0;
        bytes[base + 25] = profile.meta.presetID;
        bytes[base + 26] = profile.meta.scanlines;
        bytes[base + 27] = profile.meta.scanlinesStrength;
        bytes[base + 28] = target;
        bytes[base + 29] = profile.meta.wantVdsLineFilter;
        bytes[base + 30] = profile.meta.wantStepResponse;
        bytes[base + 31] = profile.meta.wantPeaking;
        return uploadProfileFile("slots.bin", bytes);
    })
        .then(() => fetchSlotNames())
        .then((ok) => {
        if (ok) {
            updateSlotNames();
        }
        profileNotify("Profile '" + profile.name + "' imported into slot " + PROFILE_SLOT_CHARS[target] + ". Select that slot and press load preset.");
    })
        .catch((e) => {
        profileNotify("Import failed: " + (e && e.message ? e.message : "unknown error"));
    });
};
const initProfileButtons = () => {
    const exportBtn = document.querySelector("[gbs-profile-export]");
    const importInput = document.querySelector(".gbs-profile-input");
    if (exportBtn) {
        exportBtn.addEventListener("click", exportProfile);
    }
    if (importInput) {
        importInput.addEventListener("change", (event) => {
            const file = event.target.files[0];
            if (!file) {
                return;
            }
            const reader = new FileReader();
            reader.addEventListener("load", () => {
                importProfileText(String(reader.result));
            });
            reader.readAsText(file);
            importInput.value = "";
        });
    }
};
const initLogModeButtons = () => {
    document.querySelectorAll("[gbs-log-mode]").forEach((btn) => {
        btn.addEventListener("click", () => {
            loadUser(btn.getAttribute("gbs-log-mode"));
        });
    });
};
const initClearButton = () => {
    GBSControl.ui.outputClear.addEventListener("click", () => {
        GBSControl.ui.terminal.value = "";
    });
};
const initControlMobileKeys = () => {
    const controls = document.querySelectorAll("[gbs-control-target]");
    const controlsKeys = document.querySelectorAll("[gbs-control-key]");
    controls.forEach((control) => {
        control.addEventListener("click", () => {
            GBSControl.controlKeysMobileMode = control.getAttribute("gbs-control-target");
            controls.forEach((crtl) => {
                crtl.removeAttribute("active");
            });
            control.setAttribute("active", "");
        });
    });
    controlsKeys.forEach((control) => {
        bindPressRepeat(control, controlClick(control));
    });
};
const initLegendHelpers = () => {
    nodelistToArray(document.querySelectorAll(".gbs-fieldset__legend--help")).forEach((e) => {
        e.addEventListener("click", toggleHelp);
    });
};
const initUnloadListener = () => {
    window.addEventListener("unload", () => {
        clearInterval(GBSControl.wsCheckTimer);
        if (GBSControl.ws) {
            if (GBSControl.ws.readyState == 0 || GBSControl.ws.readyState == 1) {
                GBSControl.ws.close();
            }
        }
    });
};
const initSlotButtons = () => {
    GBSControl.ui.slotContainer.innerHTML = getSlotsHTML();
    GBSControl.ui.slotButtonList = nodelistToArray(document.querySelectorAll('[gbs-role="slot"]'));
};
const initUIElements = () => {
    GBSControl.ui = {
        terminal: document.getElementById("outputTextArea"),
        webSocketConnectionWarning: document.getElementById("websocketWarning"),
        presetButtonList: nodelistToArray(document.querySelectorAll("[gbs-role='preset']")),
        slotButtonList: nodelistToArray(document.querySelectorAll('[gbs-role="slot"]')),
        toggleList: document.querySelectorAll("[gbs-toggle]"),
        toggleSwichList: document.querySelectorAll("[gbs-toggle-switch]"),
        wifiList: document.querySelector("[gbs-wifi-list]"),
        wifiListTable: document.querySelector(".gbs-wifi__list"),
        wifiConnect: document.querySelector(".gsb-wifi__connect"),
        wifiConnectButton: document.querySelector("[gbs-wifi-connect-button]"),
        wifiSSDInput: document.querySelector('[gbs-input="ssid"]'),
        wifiPasswordInput: document.querySelector('[gbs-input="password"]'),
        wifiApButton: document.querySelector("[gbs-wifi-ap]"),
        wifiStaButton: document.querySelector("[gbs-wifi-station]"),
        wifiStaSSID: document.querySelector("[gbs-wifi-station-ssid]"),
        loader: document.querySelector(".gbs-loader"),
        progressBackup: document.querySelector("[gbs-progress-backup]"),
        progressRestore: document.querySelector("[gbs-progress-restore]"),
        outputClear: document.querySelector("[gbs-output-clear]"),
        slotContainer: document.querySelector("[gbs-slot-html]"),
        backupButton: document.querySelector(".gbs-backup-button"),
        backupInput: document.querySelector(".gbs-backup-input"),
        developerSwitch: document.querySelector("[gbs-dev-switch]"),
        customSlotFilters: document.querySelector("[gbs-slot-custom-filters]"),
        alert: document.querySelector('section[name="alert"]'),
        alertOk: document.querySelector("[gbs-alert-ok]"),
        alertContent: document.querySelector("[gbs-alert-content]"),
        confirmBox: document.querySelector('section[name="confirm"]'),
        confirmOk: document.querySelector("[gbs-confirm-ok]"),
        confirmCancel: document.querySelector("[gbs-confirm-cancel]"),
        confirmContent: document.querySelector("[gbs-confirm-content]"),
        prompt: document.querySelector('section[name="prompt"]'),
        promptOk: document.querySelector("[gbs-prompt-ok]"),
        promptCancel: document.querySelector("[gbs-prompt-cancel]"),
        promptContent: document.querySelector("[gbs-prompt-content]"),
        promptInput: document.querySelector('[gbs-input="prompt-input"]'),
    };
};
const initGeneralListeners = () => {
    window.addEventListener("resize", () => {
        updateViewPort();
    });
    GBSControl.ui.backupInput.addEventListener("change", (event) => {
        const fileList = event.target["files"];
        const chosen = fileList && fileList[0];
        GBSControl.ui.backupInput.value = "";
        if (!chosen) {
            return;
        }
        // v107l: a restore replaces every setting and profile on the device, so it asks first
        gbsConfirm(`¿Restaurar la copia «${chosen.name}»? Se sustituyen todos los ajustes y presets del GBS-C y se reinicia.`).then(() => readLocalFile(chosen), () => { });
    });
    GBSControl.ui.backupButton.addEventListener("click", doBackup);
    GBSControl.ui.wifiListTable.addEventListener("click", wifiSelectSSID);
    GBSControl.ui.wifiConnectButton.addEventListener("click", wifiConnect);
    GBSControl.ui.wifiApButton.addEventListener("click", wifiSetAPMode);
    GBSControl.ui.wifiStaButton.addEventListener("click", wifiScanSSID);
    GBSControl.ui.developerSwitch.addEventListener("click", toggleDeveloperMode);
    if (GBSControl.ui.customSlotFilters)
        GBSControl.ui.customSlotFilters.addEventListener("click", toggleCustomSlotFilters);
    GBSControl.ui.alertOk.addEventListener("click", () => {
        GBSControl.ui.alert.setAttribute("hidden", "");
        gbsAlertPromise.resolve();
    });
    // v73: confirmation for the buttons that overwrite or erase something
    GBSControl.ui.confirmOk.addEventListener("click", () => {
        gbsConfirmPromise.resolve();
    });
    GBSControl.ui.confirmCancel.addEventListener("click", () => {
        gbsConfirmPromise.reject();
    });
    GBSControl.ui.promptOk.addEventListener("click", () => {
        GBSControl.ui.prompt.setAttribute("hidden", "");
        const value = GBSControl.ui.promptInput.value;
        if (value !== undefined || value.length > 0) {
            gbsPromptPromise.resolve(GBSControl.ui.promptInput.value);
        }
        else {
            gbsPromptPromise.reject();
        }
    });
    GBSControl.ui.promptCancel.addEventListener("click", () => {
        GBSControl.ui.prompt.setAttribute("hidden", "");
        gbsPromptPromise.reject();
    });
    GBSControl.ui.promptInput.addEventListener("keydown", (event) => {
        if (event.keyCode === 13) {
            GBSControl.ui.prompt.setAttribute("hidden", "");
            const value = GBSControl.ui.promptInput.value;
            if (value !== undefined || value.length > 0) {
                gbsPromptPromise.resolve(GBSControl.ui.promptInput.value);
            }
            else {
                gbsPromptPromise.reject();
            }
        }
        if (event.keyCode === 27) {
            gbsPromptPromise.reject();
        }
    });
};
const initDeveloperMode = () => {
    const devMode = GBSStorage.read("developerMode");
    if (devMode === undefined) {
        GBSStorage.write("developerMode", false);
        updateDeveloperMode(false);
    }
    else {
        updateDeveloperMode(devMode);
    }
};
const initHelp = () => {
    let help = GBSStorage.read("help");
    if (help === undefined) {
        help = false;
        GBSStorage.write("help", help);
    }
    updateHelp(help);
};
const gbsAlertPromise = {
    resolve: null,
    reject: null,
};
const alertKeyListener = (event) => {
    if (event.keyCode === 13) {
        gbsAlertPromise.resolve();
    }
    if (event.keyCode === 27) {
        gbsAlertPromise.reject();
    }
};
const gbsAlert = (text) => {
    GBSControl.ui.alertContent.textContent = text;
    GBSControl.ui.alert.removeAttribute("hidden");
    document.addEventListener("keyup", alertKeyListener);
    return new Promise((resolve, reject) => {
        gbsAlertPromise.resolve = (e) => {
            document.removeEventListener("keyup", alertKeyListener);
            GBSControl.ui.alert.setAttribute("hidden", "");
            return resolve(e);
        };
        gbsAlertPromise.reject = () => {
            document.removeEventListener("keyup", alertKeyListener);
            GBSControl.ui.alert.setAttribute("hidden", "");
            return reject();
        };
    });
};
// v73: a yes / no box of our own. The browser's confirm() blocks the page, looks wrong on a phone and some browsers
// suppress it, so this reuses the same modal machinery as the alert and the prompt.
const gbsConfirmPromise = {
    resolve: null,
    reject: null,
};
const confirmKeyListener = (event) => {
    if (event.keyCode === 13) {
        gbsConfirmPromise.resolve();
    }
    if (event.keyCode === 27) {
        gbsConfirmPromise.reject();
    }
};
// v78: the confirmation text can carry {profile}, replaced with the console profile the status bar is showing. These
// buttons act on the slot of the active profile, and half the scares come from having a different one selected than you
// think, so the question has to name it.
// v107p: whose picture memory is in use, named for the colour tab and its confirmation
const SETTINGS_NAMES = ["consola desconocida", "Mega Drive", "SNES", "PC Engine"].concat(EMPTY_PROFILES, ["Otras"]); // (after v107v) 4..17, (after v108e) 18
let settingsName = "";
const activeProfileName = () => {
    if (settingsName)
        return settingsName;
    const bar = document.getElementById("gbsStatusBar");
    const text = (bar && bar.textContent) || "";
    // v90: [^|\n]+ now that the bar has line breaks, or the profile name swallows the whole next line
    // v107h: the bar is shown in Spanish now ("perfil auto SNES"), so read that too, and drop the "auto"
    const m = text.match(/\|\s*(?:profile|perfil)\s+(?:auto\s+)?([^|\n]+)/i);
    if (!m) {
        return "the active console profile";
    }
    const name = m[1].trim();
    return name.length > 0 ? name : "the active console profile";
};
// (after v108f) «valores de fábrica»: everything the user did goes (colours and filters of each console, sync states,
// edges, last modes, preferences). Then a second question for the personal presets, with its own buttons.
const factoryReset = () => {
    gbsConfirm("¿Volver a los valores de fábrica? Se borra todo lo que has ajustado: colores y filtros de cada consola, bordes, estados de sync, último modo de cada consola y preferencias del aparato. El wifi se conserva. La placa se reiniciará.").then(() => {
        const cancel = document.querySelector("[gbs-confirm-cancel]");
        const before = cancel ? cancel.textContent : "";
        if (cancel)
            cancel.textContent = "conservarlos";
        const restore = () => {
            if (cancel)
                cancel.textContent = before;
        };
        gbsConfirm("¿Borrar también tus presets personales? Si quieres conservar alguno, expórtalo antes.").then(() => {
            restore();
            loadImg("76");
        }, () => {
            restore();
            loadUser("1");
        });
    }, () => { });
};
const gbsConfirm = (text) => {
    text = text.replace(/\{profile\}/g, activeProfileName());
    GBSControl.ui.confirmContent.textContent = text;
    // v107l: when the question is about deleting or replacing something, the button that does it is red, not green
    const risky = /borrar|sobrescrib|restaurar|fábrica|escribir la receta|reiniciar|sustituir el perfil/i.test(text);
    const okBtn = document.querySelector("[gbs-confirm-ok]");
    if (okBtn) {
        risky ? okBtn.classList.add("gbs-modal__risky") : okBtn.classList.remove("gbs-modal__risky");
        // v107p: the button says what it does: reiniciar, borrar or sustituir
        okBtn.textContent = risky
            ? /^¿volver a los valores de fábrica/i.test(text) ? "restablecer" : /^¿reiniciar/i.test(text) ? "reiniciar" : /^¿volver a(l borde| los valores) del firmware/i.test(text) ? "deshacer" : /borrar/i.test(text) ? "borrar" : "sí, sustituir"
            : "aceptar";
    }
    GBSControl.ui.confirmBox.removeAttribute("hidden");
    document.addEventListener("keyup", confirmKeyListener);
    return new Promise((resolve, reject) => {
        gbsConfirmPromise.resolve = (e) => {
            document.removeEventListener("keyup", confirmKeyListener);
            GBSControl.ui.confirmBox.setAttribute("hidden", "");
            return resolve(e);
        };
        gbsConfirmPromise.reject = () => {
            document.removeEventListener("keyup", confirmKeyListener);
            GBSControl.ui.confirmBox.setAttribute("hidden", "");
            return reject();
        };
    });
};
const gbsPromptPromise = {
    resolve: null,
    reject: null,
};
const gbsPrompt = (text, defaultValue = "") => {
    GBSControl.ui.promptContent.textContent = text;
    GBSControl.ui.prompt.removeAttribute("hidden");
    GBSControl.ui.promptInput.value = defaultValue;
    return new Promise((resolve, reject) => {
        gbsPromptPromise.resolve = resolve;
        gbsPromptPromise.reject = reject;
        GBSControl.ui.promptInput.focus();
    });
};
// (after v107v) THE COLOUR AND FILTERS OF ONE CONSOLE, TO A FILE AND BACK. His request: one file per console, in case one
// day it is shared with others. Export asks the board for the active console's record (/colorexp.txt), then wraps it in a
// small JSON file with the console's name. Import reads such a file, asks first (it replaces what that console has saved)
// and hands it to the board, which puts it into THE CONSOLE NAMED IN THE FILE and applies it if that console is on.
const COLOR_FILE_SLUGS = ["consola-desconocida", "mega-drive", "snes", "pc-engine"].concat(EMPTY_PROFILES.map((n) => n.toLowerCase().replace(/[^a-z0-9]+/g, "-")), ["otras"]); // (after v107v) "neo-geo-cd"...; (after v108e) 18 "otras"
const COLOR_RECORD_HEX = 68; // 34 bytes
const colorExport = () => {
    loadImg("58");
    setTimeout(() => {
        fetch(`/filesystem/download?file=${encodeURIComponent("/colorexp.txt")}&${+new Date()}`)
            .then((r) => {
            if (!r.ok)
                throw new Error("download");
            return r.text();
        })
            .then((text) => {
            const lines = String(text).split("\n").map((l) => l.trim());
            const slot = parseInt(lines[0], 10);
            const data = (lines[1] || "").toUpperCase();
            const name = SETTINGS_NAMES[slot] || "consola desconocida";
            if (data.length !== COLOR_RECORD_HEX || !/^[0-9A-F]+$/.test(data) || data.slice(0, 2) === "00") {
                gbsAlert(`El perfil de ${name} no tiene nada guardado. Pulsa GUARDAR antes de exportar.`);
                return;
            }
            const payload = { gbsColor: 1, firmware: "3x4x-v108h", consola: name, perfil: slot, datos: data };
            downloadBlob(new Blob([JSON.stringify(payload)], { type: "application/json" }), `color-${COLOR_FILE_SLUGS[slot] || "consola"}.gbsc`);
        })
            .catch(() => gbsAlert("No se ha podido exportar. Inténtalo otra vez."));
    }, 900);
};
const colorImportText = (text) => {
    let p = null;
    try {
        p = JSON.parse(text);
    }
    catch (e) {
        p = null;
    }
    if (!p || p.gbsColor !== 1 || !Number.isInteger(p.perfil) || p.perfil < 0 || p.perfil >= SETTINGS_NAMES.length ||
        typeof p.datos !== "string" || p.datos.length !== COLOR_RECORD_HEX || !/^[0-9A-F]+$/i.test(p.datos)) {
        gbsAlert("El archivo no es un perfil de color y filtro válido.");
        return;
    }
    const name = SETTINGS_NAMES[p.perfil] || "consola desconocida";
    gbsConfirm(`¿Sustituir el perfil de color y filtro de ${name} por el del archivo? Se perderá lo que tiene guardado ahora.`).then(() => {
        const fd = new FormData();
        fd.append("file", new Blob([`${p.perfil}\n${String(p.datos).toUpperCase()}\n`], { type: "application/octet-stream" }), "colorimp.txt");
        return fetch("/filesystem/upload", { method: "POST", body: fd })
            .then((r) => {
            if (!r.ok)
                throw new Error("upload");
            loadImg("59");
            gbsAlert(`Perfil de color y filtro de ${name} importado.`);
        })
            .catch(() => gbsAlert("No se ha podido importar. Inténtalo otra vez."));
    }, () => { });
};
// (after v107v) "Otras consolas": folded unless one of them is in use or the user opens it
const consoleOthersShow = (open) => {
    const grid = document.getElementById("workConsoleOthers");
    const btn = document.getElementById("workConsoleMore");
    if (!grid || !btn)
        return;
    open ? grid.removeAttribute("hidden") : grid.setAttribute("hidden", "");
    btn.textContent = open ? "Otras consolas ▾" : "Otras consolas ▸";
};
const initConsoleOthers = () => {
    const btn = document.getElementById("workConsoleMore");
    const grid = document.getElementById("workConsoleOthers");
    if (btn && grid)
        btn.addEventListener("click", () => consoleOthersShow(grid.hasAttribute("hidden")));
};
const initColorFileButtons = () => {
    nodelistToArray(document.querySelectorAll(".gbs-col-export")).forEach((b) => b.addEventListener("click", colorExport));
    nodelistToArray(document.querySelectorAll(".gbs-col-import")).forEach((b) => b.addEventListener("click", () => {
        const inp = document.createElement("input");
        inp.type = "file";
        inp.accept = ".gbsc,application/json";
        inp.addEventListener("change", () => {
            const f = inp.files && inp.files[0];
            if (!f)
                return;
            const rd = new FileReader();
            rd.onload = () => colorImportText(String(rd.result));
            rd.readAsText(f);
        });
        inp.click();
    }));
};
const initUI = () => {
    updateCustomSlotFilters();
    initGeneralListeners();
    updateViewPort();
    initSlotButtons();
    initLegendHelpers();
    initFirmwareUpdate();
    initFileDrop();
    initMenuButtons();
    initWorkScreen();
    const factoryBtn = document.getElementById("sysFactory"); // (after v108f)
    if (factoryBtn)
        factoryBtn.addEventListener("click", factoryReset);
    initGBSButtons();
    initClearButton();
    initLogModeButtons();
    initDiagnosticsButton();
    initProfileButtons();
    initColorFileButtons(); // (after v107v)
    initConsoleOthers(); // (after v107v)
    initControlMobileKeys();
    initUnloadListener();
    initDeveloperMode();
    initHelp();
};
// v107m: the old page showed the browser's own file input, so a file could be dragged onto it. The new buttons hide it
// (display:none), which also drops drag and drop; this gives it back to every button that wraps a hidden file input.
const initFileDrop = () => {
    document.querySelectorAll(".gbs-sys__file, .gbs-import").forEach((zone) => {
        const input = zone.querySelector('input[type="file"]');
        if (!input) {
            return;
        }
        const over = (e) => {
            e.preventDefault();
            zone.classList.add("gbs-drop");
        };
        zone.addEventListener("dragenter", over);
        zone.addEventListener("dragover", over);
        zone.addEventListener("dragleave", () => zone.classList.remove("gbs-drop"));
        zone.addEventListener("drop", (e) => {
            e.preventDefault();
            zone.classList.remove("gbs-drop");
            if (!e.dataTransfer || !e.dataTransfer.files.length) {
                return;
            }
            input.files = e.dataTransfer.files;
            input.dispatchEvent(new Event("change"));
        });
    });
};
const initFirmwareUpdate = () => {
    const file = document.querySelector("[gbs-update-file]");
    const btn = document.querySelector("[gbs-update-upload]");
    const status = document.querySelector("[gbs-update-status]");
    const barWrap = document.querySelector("[gbs-update-bar-wrap]");
    const bar = document.querySelector("[gbs-update-bar]");
    if (!file || !btn) {
        return;
    }
    // v107h: the chosen file's name, under the button, since the browser's own file input is hidden now
    const fname = document.querySelector("[gbs-update-filename]");
    file.addEventListener("change", () => {
        if (fname)
            fname.textContent = file.files && file.files[0] ? file.files[0].name : "ninguno elegido";
    });
    btn.addEventListener("click", () => {
        const f = file.files && file.files[0];
        if (!f) {
            status.textContent = "Elige primero un archivo .bin.";
            return;
        }
        if (!/\.bin$/i.test(f.name)) {
            status.textContent = "El archivo tiene que ser un firmware .bin.";
            return;
        }
        if (!confirm(`¿Actualizar el firmware con ${f.name}? El aparato se reiniciará.`)) {
            return;
        }
        const data = new FormData();
        data.append("firmware", f, f.name);
        const xhr = new XMLHttpRequest();
        xhr.open("POST", `http://${GBSControl.serverIP}/update`);
        barWrap.hidden = false;
        btn.disabled = true;
        xhr.upload.onprogress = (e) => {
            if (e.lengthComputable) {
                const pct = Math.round((e.loaded / e.total) * 100);
                bar.style.width = pct + "%";
                status.textContent = `Subiendo... ${pct}%`;
            }
        };
        xhr.onload = () => {
            btn.disabled = false;
            const ok = xhr.status === 200 && xhr.responseText.indexOf("Update OK") >= 0;
            status.textContent = ok
                ? "Actualizado. El aparato se está reiniciando: recarga esta página en unos 20 segundos."
                : "La actualización falló o el archivo no era válido. No se ha cambiado nada.";
        };
        xhr.onerror = () => {
            btn.disabled = false;
            status.textContent = "Se ha perdido la conexión. Si la subida había terminado, el aparato puede estar reiniciándose.";
        };
        status.textContent = "Subiendo... 0%";
        xhr.send(data);
    });
};
const main = () => {
    const ip = location.hostname;
    GBSControl.serverIP = ip;
    GBSControl.webSocketServerUrl = `ws://${ip}:81/`;
    document
        .querySelector(".gbs-loader img")
        .setAttribute("src", document.head
        .querySelector('[rel="apple-touch-icon"]')
        .getAttribute("href"));
    fetchSlotNamesAndInit();
};
main();
