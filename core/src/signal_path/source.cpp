#include <server.h>
#include <signal_path/source.h>
#include <utils/flog.h>
#include <signal_path/signal_path.h>
#include <core.h>
#include <gui/gui.h>

SourceManager::SourceManager() {
}

void SourceManager::registerSource(std::string name, SourceHandler* handler) {
    if (sources.find(name) != sources.end()) {
        flog::error("Tried to register new source with existing name: {0}", name);
        return;
    }
    sources[name] = handler;
    onSourceRegistered.emit(name);
}

void SourceManager::unregisterSource(std::string name) {
    if (sources.find(name) == sources.end()) {
        flog::error("Tried to unregister non existent source: {0}", name);
        return;
    }
    onSourceUnregister.emit(name);
    if (name == selectedName) {
        if (selectedHandler != NULL) {
            sources[selectedName]->deselectHandler(sources[selectedName]->ctx);
        }
        sigpath::iqFrontEnd.setInput(&nullSource);
        selectedHandler = NULL;
    }
    sources.erase(name);
    onSourceUnregistered.emit(name);
}

std::vector<std::string> SourceManager::getSourceNames() {
    std::vector<std::string> names;
    for (auto const& [name, src] : sources) { names.push_back(name); }
    return names;
}

void SourceManager::selectSource(std::string name) {
    if (core::isTuningLocked()) { return; }
    if (sources.find(name) == sources.end()) {
        flog::error("Tried to select non existent source: {0}", name);
        return;
    }
    if (selectedHandler != NULL) {
        sources[selectedName]->deselectHandler(sources[selectedName]->ctx);
    }
    selectedHandler = sources[name];
    selectedHandler->selectHandler(selectedHandler->ctx);
    selectedName = name;
    if (core::args["server"].b()) {
        server::setInput(selectedHandler->stream);
    }
    else {
        sigpath::iqFrontEnd.setInput(selectedHandler->stream);
    }
    // Set server input here
}

void SourceManager::showSelectedMenu() {
    if (selectedHandler == NULL) {
        return;
    }
    ImGui::BeginDisabled(core::isTuningLocked());
    selectedHandler->menuHandler(selectedHandler->ctx);
    ImGui::EndDisabled();
}

void SourceManager::start() {
    if (selectedHandler == NULL) {
        return;
    }
    selectedHandler->startHandler(selectedHandler->ctx);
}

void SourceManager::stop() {
    if (core::isSessionLocked()) { return; }
    if (selectedHandler == NULL) {
        return;
    }
    selectedHandler->stopHandler(selectedHandler->ctx);
}

void SourceManager::tune(double freq) {
    if (core::isTuningLocked() && freq != currentFreq) { return; }
    if (selectedHandler == NULL) {
        return;
    }
    // TODO: No need to always retune the hardware in Panadapter mode
    selectedHandler->tuneHandler(abs(((tuneMode == TuningMode::NORMAL) ? (freq + tuneOffset) : ifFreq)), selectedHandler->ctx);
    onRetune.emit(freq + tuneOffset);
    currentFreq = freq;
}

void SourceManager::setTuningOffset(double offset) {
    if (core::isTuningLocked()) { return; }
    tuneOffset = offset;
    tune(currentFreq);
}

void SourceManager::setTuningMode(TuningMode mode) {
    if (core::isTuningLocked()) { return; }
    tuneMode = mode;
    tune(currentFreq);
}

void SourceManager::setPanadapterIF(double freq) {
    if (core::isTuningLocked()) { return; }
    ifFreq = freq;
    tune(currentFreq);
}
