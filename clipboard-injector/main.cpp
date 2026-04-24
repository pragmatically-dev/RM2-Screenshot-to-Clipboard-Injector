#include <QQmlApplicationEngine>
#include "ClipboardInjector.hpp"

extern "C" void registerQmldiff() {
	qmlRegisterSingletonInstance<ClipboardInjector>(
			"dev.pragmatically.clipboardinjector", 0, 1,
			"ClipboardInjector", new ClipboardInjector());
}
