import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');

const xml = (value) =>
  String(value)
    .replaceAll('&', '&amp;')
    .replaceAll('<', '&lt;')
    .replaceAll('>', '&gt;')
    .replaceAll('"', '&quot;');

const indent = (value, spaces = 4) =>
  value
    .split('\n')
    // Не добавляем отступ к тексту внутри <data>: пробелы становятся частью
    // Cyberiada-параметров (`type/ ...`) и ломают GraphMlParser.
    .map((line) =>
      line && line.trimStart().startsWith('<') ? `${' '.repeat(spaces)}${line}` : line
    )
    .join('\n');

function data(key, value) {
  return `<data key="${key}">${xml(value)}</data>`;
}

function rect(x, y, width = 360, height = 90) {
  return `<data key="dGeometry"><rect x="${x}" y="${y}" width="${width}" height="${height}"></rect></data>`;
}

function state(id, name, actions = '', x = 0, y = 0) {
  return `<node id="${id}">
  ${data('dName', name)}
  ${data('dData', actions)}
  ${rect(x, y)}
  ${data('dColor', '#FFFFFF')}
</node>`;
}

function vertex(id, type, x = 0, y = 0) {
  return `<node id="${id}">
  ${data('dVertex', type)}
  <data key="dGeometry"><rect x="${x}" y="${y}" width="-1" height="-1"></rect></data>
</node>`;
}

function note(id, text, x = 0, y = 0) {
  return `<node id="${id}">
  ${rect(x, y, 420, 80)}
  ${data('dNote', 'informal')}
  ${data('dData', text)}
</node>`;
}

function component(id, type, parameters = {}) {
  const values = [`id/ ${id}`, `type/ ${type}`];
  for (const [key, value] of Object.entries(parameters)) values.push(`${key}/ ${value}`);
  return `<node id="component_${id}">
  ${data('dNote', 'formal')}
  ${data('dName', 'CGML_COMPONENT')}
  ${data('dData', `${values.join('\n\n')}\n`)}
  <data key="dLapkiSchemePosition"><point x="30" y="30"></point></data>
</node>`;
}

let edgeNumber = 0;
function edge(source, target, trigger = '', condition = '', actions = '') {
  edgeNumber += 1;
  let body = '';
  if (trigger || condition || actions) {
    const label = `${trigger}${condition ? `[${condition}]` : ''}/\n${actions}`;
    body = `\n  ${data('dData', label)}\n  <data key="dLabelGeometry"><point x="0" y="0"></point></data>\n`;
  }
  return `<edge id="edge_${edgeNumber}" source="${source}" target="${target}">${body}</edge>`;
}

function meta(platform) {
  return `<node id="coreMeta">
  ${data('dNote', 'formal')}
  ${data('dName', 'CGML_META')}
  ${data(
    'dData',
    `platform/ ${platform}\n\nstandardVersion/ 1.0\n\nplatformVersion/ 1.0\n\nlapkiVisual/ true\n`
  )}
</node>`;
}

function graph(id, name, platform, nodes, edges) {
  return `<graph id="${id}">
  ${data('dStateMachine', '')}
  ${name ? data('dName', name) : ''}
  ${rect(0, 0, 450, 100)}
${indent(meta(platform), 2)}
${nodes.map((node) => indent(node, 2)).join('\n')}
${edges.map((item) => indent(item, 2)).join('\n')}
</graph>`;
}

const entry = (actions) => `entry/\n${actions}`;

function sensorMachine() {
  const nodes = [
    state('sensor_idle', 'Ожидание POLL_SENSORS', entry('SensorBus.setAddress(13);'), 0, 0),
    state('sensor_poll_wait', 'Ожидание OK после POLL_SENSORS', '', 420, 0),
    state('sensor_read_wait', 'Приём READ_SENSORS по позиции', '', 840, 0),
    vertex('sensor_first_choice', 'choice', 1250, 20),
    vertex('sensor_status_choice', 'choice', 1410, 20),
    vertex('sensor_count_choice', 'choice', 1570, 20),
    state(
      'sensor_send_error',
      'Передача ошибки центральной плате',
      entry('SensorBus.sendPacket(12, SensorArduino.lastData);\nSensorStep.call();'),
      1260,
      170
    ),
    state(
      'sensor_send_start',
      'Начало позиционной передачи',
      entry('SensorIndex.reset();\nSensorStep.call();'),
      1680,
      170
    ),
    state(
      'sensor_send_byte',
      'Передача следующего байта',
      entry(
        'SensorResponse.peek(SensorIndex.value);\nSensorBus.sendPacket(12, SensorResponse.value);\nSensorIndex.add(1);\nSensorStep.call();'
      ),
      1680,
      310
    ),
    vertex('sensor_send_choice', 'choice', 2090, 330),
    vertex('sensor_initial', 'initial', -80, 20),
    note(
      'sensor_note',
      'POLL_SENSORS=0x01, затем READ_SENSORS=0x04. Успех READ: ровно 9 полей и OK. Значения полей не проверяются как маркеры.',
      420,
      470
    ),
    component('SensorBus', 'SimpleBus'),
    component('SensorArduino', 'ArduinoAdapter'),
    component('SensorTimeout', 'Timer'),
    component('SensorStep', 'UserSignal'),
    component('SensorIndex', 'Counter', { name: 'Индекс байта READ_SENSORS' }),
    component('SensorResponse', 'Array', { size: 10, name: 'Ответ READ_SENSORS' }),
  ];

  const edges = [
    edge('sensor_initial', 'sensor_idle'),
    edge(
      'sensor_idle',
      'sensor_poll_wait',
      'SensorBus.packetReceived',
      'SensorBus.lastData == 1',
      'SensorArduino.sendPacket(1);\nSensorTimeout.start(2000);'
    ),
    edge(
      'sensor_poll_wait',
      'sensor_read_wait',
      'SensorArduino.packetReceived',
      'SensorArduino.lastData == 3',
      'SensorArduino.sendPacket(4);\nSensorIndex.reset();\nSensorTimeout.start(2000);'
    ),
    edge(
      'sensor_poll_wait',
      'sensor_send_error',
      'SensorArduino.packetReceived',
      'SensorArduino.lastData != 3'
    ),
    edge(
      'sensor_read_wait',
      'sensor_first_choice',
      'SensorArduino.packetReceived',
      '',
      'SensorResponse.set(SensorIndex.value, SensorArduino.lastData);\nSensorIndex.add(1);\nSensorTimeout.start(2000);'
    ),
    edge('sensor_first_choice', 'sensor_status_choice', '', 'SensorIndex.value == 1'),
    edge('sensor_first_choice', 'sensor_count_choice', '', 'else'),
    edge('sensor_status_choice', 'sensor_send_error', '', 'SensorArduino.lastData == 224'),
    edge('sensor_status_choice', 'sensor_send_error', '', 'SensorArduino.lastData == 225'),
    edge('sensor_status_choice', 'sensor_send_error', '', 'SensorArduino.lastData == 226'),
    edge('sensor_status_choice', 'sensor_send_error', '', 'SensorArduino.lastData == 227'),
    edge('sensor_status_choice', 'sensor_count_choice', '', 'else'),
    edge('sensor_count_choice', 'sensor_send_start', '', 'SensorIndex.value == 10'),
    edge('sensor_count_choice', 'sensor_read_wait', '', 'else'),
    edge('sensor_send_error', 'sensor_idle', 'SensorStep.isCalled'),
    edge('sensor_send_start', 'sensor_send_byte', 'SensorStep.isCalled'),
    edge('sensor_send_byte', 'sensor_send_choice', 'SensorStep.isCalled'),
    edge('sensor_send_choice', 'sensor_idle', '', 'SensorIndex.value == 10'),
    edge('sensor_send_choice', 'sensor_send_byte', '', 'else'),
    edge('sensor_poll_wait', 'sensor_idle', 'SensorTimeout.timeout'),
    edge('sensor_read_wait', 'sensor_idle', 'SensorTimeout.timeout'),
  ];
  return graph('MS1GateSensors', 'Датчики — мост фиксированного протокола', 'tjc-ms1-ard-a1', nodes, edges);
}

const commandPayloadEdges = (choice, receive, forward, invalid, prefix) => [
  edge(choice, forward, '', `${prefix}Command.value == 1`, `${prefix}Expected.reset();`),
  edge(choice, receive, '', `${prefix}Command.value == 6`, `${prefix}Expected.set(5);\n${prefix}Index.reset();`),
  edge(choice, receive, '', `${prefix}Command.value == 8`, `${prefix}Expected.set(1);\n${prefix}Index.reset();`),
  edge(choice, receive, '', `${prefix}Command.value == 9`, `${prefix}Expected.set(8);\n${prefix}Index.reset();`),
  edge(choice, receive, '', `${prefix}Command.value == 10`, `${prefix}Expected.set(6);\n${prefix}Index.reset();`),
  edge(choice, receive, '', `${prefix}Command.value == 11`, `${prefix}Expected.set(2);\n${prefix}Index.reset();`),
  edge(choice, receive, '', `${prefix}Command.value == 12`, `${prefix}Expected.set(2);\n${prefix}Index.reset();`),
  edge(choice, receive, '', `${prefix}Command.value == 13`, `${prefix}Expected.set(4);\n${prefix}Index.reset();`),
  edge(choice, receive, '', `${prefix}Command.value == 14`, `${prefix}Expected.set(1);\n${prefix}Index.reset();`),
  edge(choice, forward, '', `${prefix}Command.value == 15`, `${prefix}Expected.reset();`),
  edge(choice, forward, '', `${prefix}Command.value == 16`, `${prefix}Expected.reset();`),
  edge(choice, invalid, '', 'else'),
];

function actuatorMachine() {
  const nodes = [
    state('act_idle', 'Ожидание команды', entry('ActBus.setAddress(14);'), 0, 0),
    vertex('act_command_choice', 'choice', 400, 20),
    state('act_receive_payload', 'Приём payload по позиции', '', 560, 0),
    vertex('act_payload_choice', 'choice', 960, 20),
    state(
      'act_forward_start',
      'Передача к Arduino: код',
      entry(
        'ActArduino.sendPacket(ActCommand.value);\nActIndex.reset();\nActTimeout.start(2000);\nActStep.call();'
      ),
      1120,
      0
    ),
    vertex('act_forward_choice', 'choice', 1520, 20),
    state(
      'act_forward_byte',
      'Передача к Arduino: payload',
      entry(
        'ActRequest.peek(ActIndex.value);\nActArduino.sendPacket(ActRequest.value);\nActIndex.add(1);\nActStep.call();'
      ),
      1680,
      0
    ),
    vertex('act_forward_count', 'choice', 2080, 20),
    vertex('act_response_length', 'choice', 2240, 20),
    state('act_wait_response', 'Приём ответа Arduino по позиции', '', 2400, 0),
    vertex('act_response_first', 'choice', 2800, 20),
    vertex('act_response_status', 'choice', 2960, 20),
    vertex('act_response_count', 'choice', 3120, 20),
    state(
      'act_send_start',
      'Начало ответа центральной плате',
      entry('ActIndex.reset();\nActStep.call();'),
      3280,
      0
    ),
    state(
      'act_send_byte',
      'Передача байта ответа',
      entry(
        'ActResponse.peek(ActIndex.value);\nActBus.sendPacket(12, ActResponse.value);\nActIndex.add(1);\nActStep.call();'
      ),
      3440,
      0
    ),
    vertex('act_send_choice', 'choice', 3840, 20),
    state(
      'act_invalid',
      'Неизвестная команда',
      entry('ActBus.sendPacket(12, 224);\nActStep.call();'),
      560,
      170
    ),
    vertex('act_initial', 'initial', -80, 20),
    note(
      'act_note',
      'Команда и payload пересылаются Arduino без изменений. Ответ читается как 1, 18 или 21 байт. Ошибка E0..E3 состоит из одного байта.',
      1120,
      300
    ),
    component('ActBus', 'SimpleBus'),
    component('ActArduino', 'ArduinoAdapter'),
    component('ActTimeout', 'Timer'),
    component('ActStep', 'UserSignal'),
    component('ActCommand', 'Counter', { name: 'Код команды актуатора' }),
    component('ActExpected', 'Counter', { name: 'Длина payload' }),
    component('ActIndex', 'Counter', { name: 'Индекс байта' }),
    component('ActResponseExpected', 'Counter', { name: 'Длина ответа' }),
    component('ActRequest', 'Array', { size: 8, name: 'Payload команды' }),
    component('ActResponse', 'Array', { size: 21, name: 'Ответ Arduino' }),
  ];

  const edges = [
    edge('act_initial', 'act_idle'),
    edge(
      'act_idle',
      'act_command_choice',
      'ActBus.packetReceived',
      '',
      'ActCommand.set(ActBus.lastData);\nActTimeout.start(2000);'
    ),
    ...commandPayloadEdges(
      'act_command_choice',
      'act_receive_payload',
      'act_forward_start',
      'act_invalid',
      'Act'
    ),
    edge(
      'act_receive_payload',
      'act_payload_choice',
      'ActBus.packetReceived',
      '',
      'ActRequest.set(ActIndex.value, ActBus.lastData);\nActIndex.add(1);\nActTimeout.start(2000);'
    ),
    edge('act_payload_choice', 'act_forward_start', '', 'ActIndex.value == ActExpected.value'),
    edge('act_payload_choice', 'act_receive_payload', '', 'else'),
    edge('act_forward_start', 'act_forward_choice', 'ActStep.isCalled'),
    edge('act_forward_choice', 'act_response_length', '', 'ActExpected.value == 0'),
    edge('act_forward_choice', 'act_forward_byte', '', 'else'),
    edge('act_forward_byte', 'act_forward_count', 'ActStep.isCalled'),
    edge('act_forward_count', 'act_response_length', '', 'ActIndex.value == ActExpected.value'),
    edge('act_forward_count', 'act_forward_byte', '', 'else'),
    edge(
      'act_response_length',
      'act_wait_response',
      '',
      'ActCommand.value == 1',
      'ActResponseExpected.set(21);\nActIndex.reset();\nActTimeout.start(2000);'
    ),
    edge(
      'act_response_length',
      'act_wait_response',
      '',
      'ActCommand.value == 16',
      'ActResponseExpected.set(18);\nActIndex.reset();\nActTimeout.start(2000);'
    ),
    edge(
      'act_response_length',
      'act_wait_response',
      '',
      'else',
      'ActResponseExpected.set(1);\nActIndex.reset();\nActTimeout.start(2000);'
    ),
    edge(
      'act_wait_response',
      'act_response_first',
      'ActArduino.packetReceived',
      '',
      'ActResponse.set(ActIndex.value, ActArduino.lastData);\nActIndex.add(1);\nActTimeout.start(2000);'
    ),
    edge('act_response_first', 'act_response_status', '', 'ActIndex.value == 1'),
    edge('act_response_first', 'act_response_count', '', 'else'),
    edge('act_response_status', 'act_send_start', '', 'ActArduino.lastData == 224', 'ActResponseExpected.set(1);'),
    edge('act_response_status', 'act_send_start', '', 'ActArduino.lastData == 225', 'ActResponseExpected.set(1);'),
    edge('act_response_status', 'act_send_start', '', 'ActArduino.lastData == 226', 'ActResponseExpected.set(1);'),
    edge('act_response_status', 'act_send_start', '', 'ActArduino.lastData == 227', 'ActResponseExpected.set(1);'),
    edge('act_response_status', 'act_response_count', '', 'else'),
    edge('act_response_count', 'act_send_start', '', 'ActIndex.value == ActResponseExpected.value'),
    edge('act_response_count', 'act_wait_response', '', 'else'),
    edge('act_send_start', 'act_send_byte', 'ActStep.isCalled'),
    edge('act_send_byte', 'act_send_choice', 'ActStep.isCalled'),
    edge('act_send_choice', 'act_idle', '', 'ActIndex.value == ActResponseExpected.value'),
    edge('act_send_choice', 'act_send_byte', '', 'else'),
    edge('act_invalid', 'act_idle', 'ActStep.isCalled'),
    edge('act_receive_payload', 'act_idle', 'ActTimeout.timeout'),
    edge('act_wait_response', 'act_idle', 'ActTimeout.timeout'),
  ];
  return graph('MS1GateActuator', 'Актуатор — прозрачный мост', 'tjc-ms1-ard-a1', nodes, edges);
}

function centralMachine() {
  const nodes = [
    state(
      'main_idle',
      'Idle: управление или периодический цикл',
      entry('MainBus.setAddress(12);\nMainPeriodic.start(2000);'),
      0,
      0
    ),
    state('main_control_command', 'Ожидание кода команды ПК', '', 420, 0),
    vertex('main_command_choice', 'choice', 820, 20),
    state('main_receive_payload', 'Приём payload ПК по позиции', '', 980, 0),
    vertex('main_payload_choice', 'choice', 1380, 20),
    state(
      'main_forward_start',
      'Пересылка актуатору: код',
      entry(
        'MainBus.sendPacket(14, MainCommand.value);\nMainIndex.reset();\nMainFlow.set(1);\nMainTimeout.start(2000);\nMainStep.call();'
      ),
      1540,
      0
    ),
    vertex('main_forward_choice', 'choice', 1940, 20),
    state(
      'main_forward_byte',
      'Пересылка актуатору: payload',
      entry(
        'MainRequest.peek(MainIndex.value);\nMainBus.sendPacket(14, MainRequest.value);\nMainIndex.add(1);\nMainStep.call();'
      ),
      2100,
      0
    ),
    vertex('main_forward_count', 'choice', 2500, 20),
    vertex('main_response_length', 'choice', 2660, 20),
    state('main_wait_actuator', 'Приём ответа актуатора по позиции', '', 2820, 0),
    vertex('main_response_first', 'choice', 3220, 20),
    vertex('main_response_status', 'choice', 3380, 20),
    vertex('main_response_count', 'choice', 3540, 20),
    vertex('main_flow_choice', 'choice', 3700, 20),
    state(
      'main_send_response_start',
      'CONTROL_RESPONSE: заголовок',
      entry(
        'MainBus.sendPacket(99, 18);\nMainBus.sendPacket(99, MainCommand.value);\nMainIndex.reset();\nMainStep.call();'
      ),
      3860,
      0
    ),
    state(
      'main_send_response_byte',
      'CONTROL_RESPONSE: тело',
      entry(
        'MainResponse.peek(MainIndex.value);\nMainBus.sendPacket(99, MainResponse.value);\nMainIndex.add(1);\nMainStep.call();'
      ),
      4260,
      0
    ),
    vertex('main_send_response_choice', 'choice', 4660, 20),
    state(
      'main_invalid',
      'Ответ на неизвестную команду',
      entry(
        'MainBus.sendPacket(99, 18);\nMainBus.sendPacket(99, MainCommand.value);\nMainBus.sendPacket(99, 224);\nMainStep.call();'
      ),
      980,
      170
    ),
    state(
      'main_wait_sensors',
      'Приём READ_SENSORS по позиции',
      '',
      420,
      340
    ),
    vertex('main_sensor_first', 'choice', 820, 360),
    vertex('main_sensor_status', 'choice', 980, 360),
    vertex('main_sensor_count', 'choice', 1140, 360),
    vertex('main_sensor_final', 'choice', 1300, 360),
    state(
      'main_calculate_door',
      'Вычисление состояния двери',
      entry(
        'MainDoorSum.set(0);\nMainSensors.peek(1);\nMainDoorSum.add(MainSensors.value);\nMainSensors.peek(2);\nMainDoorSum.add(MainSensors.value);\nMainSensors.peek(3);\nMainDoorSum.add(MainSensors.value);\nMainStep.call();'
      ),
      1460,
      340
    ),
    vertex('main_door_choice', 'choice', 1860, 360),
    state(
      'main_door_closed',
      'Дверь закрыта',
      entry('MainDoorOpen.reset();\nMainDoorStage.reset();\nMainStep.call();'),
      2020,
      300
    ),
    state(
      'main_door_open',
      'Дверь открыта',
      entry('MainDoorOpen.set(1);\nMainDoorStage.set(1);\nMainStep.call();'),
      2020,
      420
    ),
    state(
      'main_send_environment',
      'UPDATE_ENVIRONMENT',
      entry(
        'MainBus.sendPacket(14, 6);\nMainSensors.peek(4);\nMainBus.sendPacket(14, MainSensors.value);\nMainSensors.peek(5);\nMainBus.sendPacket(14, MainSensors.value);\nMainSensors.peek(8);\nMainBus.sendPacket(14, MainSensors.value);\nMainBus.sendPacket(14, MainDoorOpen.value);\nMainBus.sendPacket(14, MainDoorStage.value);\nMainFlow.set(2);\nMainResponseExpected.set(1);\nMainIndex.reset();\nMainTimeout.start(2000);\nMainStep.call();'
      ),
      2420,
      340
    ),
    vertex('main_environment_status', 'choice', 3860, 360),
    state(
      'main_get_state',
      'GET_ACTUATOR_STATE',
      entry(
        'MainBus.sendPacket(14, 1);\nMainFlow.set(3);\nMainResponseExpected.set(21);\nMainIndex.reset();\nMainTimeout.start(2000);\nMainStep.call();'
      ),
      4020,
      340
    ),
    vertex('main_state_final', 'choice', 3860, 500),
    state(
      'main_prepare_telemetry',
      'Сборка 29-байтовой телеметрии',
      entry(telemetryActions()),
      4180,
      500
    ),
    state(
      'main_send_telemetry_byte',
      'Передача байта телеметрии',
      entry(
        'MainTelemetry.peek(MainIndex.value);\nMainBus.sendPacket(99, MainTelemetry.value);\nMainIndex.add(1);\nMainStep.call();'
      ),
      4580,
      500
    ),
    vertex('main_telemetry_choice', 'choice', 4980, 520),
    state(
      'main_end_telemetry',
      'Завершение телеметрии',
      entry('MainBus.sendPacket(99, 12);\nMainStep.call();'),
      5140,
      500
    ),
    vertex('main_initial', 'initial', -80, 20),
    note(
      'main_note',
      'Idle принимает только CONTROL_REQUEST=0x11. Во время запроса и телеметрии новые запросы не принимаются. Любой неполный обмен отбрасывается через 2 секунды.',
      0,
      650
    ),
    note(
      'main_door_note',
      'doorOpen=0 и doorStage=0, если все три концевика равны 1; иначе doorOpen=1 и doorStage=1.',
      440,
      650
    ),
    component('MainBus', 'SimpleBus'),
    component('MainTimeout', 'Timer'),
    component('MainPeriodic', 'Timer'),
    component('MainStep', 'UserSignal'),
    component('MainCommand', 'Counter', { name: 'Код управляющей команды' }),
    component('MainExpected', 'Counter', { name: 'Длина payload' }),
    component('MainIndex', 'Counter', { name: 'Индекс байта' }),
    component('MainResponseExpected', 'Counter', { name: 'Длина ответа' }),
    component('MainFlow', 'Counter', { name: '1 control, 2 environment, 3 state' }),
    component('MainSensorIndex', 'Counter', { name: 'Индекс ответа датчиков' }),
    component('MainDoorOpen', 'Counter', { name: 'door open' }),
    component('MainDoorStage', 'Counter', { name: 'door stage' }),
    component('MainDoorSum', 'CalcInt', { name: 'Сумма концевиков' }),
    component('MainRequest', 'Array', { size: 8, name: 'Payload ПК' }),
    component('MainResponse', 'Array', { size: 21, name: 'Ответ актуатора' }),
    component('MainSensors', 'Array', { size: 10, name: 'Ответ READ_SENSORS' }),
    component('MainTelemetry', 'Array', { size: 29, name: 'Payload телеметрии' }),
  ];

  const edges = [
    edge('main_initial', 'main_idle'),
    edge(
      'main_idle',
      'main_control_command',
      'MainBus.packetReceived',
      'MainBus.lastData == 17',
      'MainTimeout.start(2000);'
    ),
    edge(
      'main_idle',
      'main_wait_sensors',
      'MainPeriodic.timeout',
      '',
      'MainBus.sendPacket(13, 1);\nMainSensorIndex.reset();\nMainTimeout.start(2000);'
    ),
    edge(
      'main_control_command',
      'main_command_choice',
      'MainBus.packetReceived',
      '',
      'MainCommand.set(MainBus.lastData);\nMainTimeout.start(2000);'
    ),
    ...commandPayloadEdges(
      'main_command_choice',
      'main_receive_payload',
      'main_forward_start',
      'main_invalid',
      'Main'
    ),
    edge(
      'main_receive_payload',
      'main_payload_choice',
      'MainBus.packetReceived',
      '',
      'MainRequest.set(MainIndex.value, MainBus.lastData);\nMainIndex.add(1);\nMainTimeout.start(2000);'
    ),
    edge('main_payload_choice', 'main_forward_start', '', 'MainIndex.value == MainExpected.value'),
    edge('main_payload_choice', 'main_receive_payload', '', 'else'),
    edge('main_forward_start', 'main_forward_choice', 'MainStep.isCalled'),
    edge('main_forward_choice', 'main_response_length', '', 'MainExpected.value == 0'),
    edge('main_forward_choice', 'main_forward_byte', '', 'else'),
    edge('main_forward_byte', 'main_forward_count', 'MainStep.isCalled'),
    edge('main_forward_count', 'main_response_length', '', 'MainIndex.value == MainExpected.value'),
    edge('main_forward_count', 'main_forward_byte', '', 'else'),
    edge(
      'main_response_length',
      'main_wait_actuator',
      '',
      'MainCommand.value == 1',
      'MainResponseExpected.set(21);\nMainIndex.reset();\nMainTimeout.start(2000);'
    ),
    edge(
      'main_response_length',
      'main_wait_actuator',
      '',
      'MainCommand.value == 16',
      'MainResponseExpected.set(18);\nMainIndex.reset();\nMainTimeout.start(2000);'
    ),
    edge(
      'main_response_length',
      'main_wait_actuator',
      '',
      'else',
      'MainResponseExpected.set(1);\nMainIndex.reset();\nMainTimeout.start(2000);'
    ),
    edge(
      'main_wait_actuator',
      'main_response_first',
      'MainBus.packetReceived',
      '',
      'MainResponse.set(MainIndex.value, MainBus.lastData);\nMainIndex.add(1);\nMainTimeout.start(2000);'
    ),
    edge('main_response_first', 'main_response_status', '', 'MainIndex.value == 1'),
    edge('main_response_first', 'main_response_count', '', 'else'),
    edge('main_response_status', 'main_flow_choice', '', 'MainBus.lastData == 224', 'MainResponseExpected.set(1);'),
    edge('main_response_status', 'main_flow_choice', '', 'MainBus.lastData == 225', 'MainResponseExpected.set(1);'),
    edge('main_response_status', 'main_flow_choice', '', 'MainBus.lastData == 226', 'MainResponseExpected.set(1);'),
    edge('main_response_status', 'main_flow_choice', '', 'MainBus.lastData == 227', 'MainResponseExpected.set(1);'),
    edge('main_response_status', 'main_response_count', '', 'else'),
    edge('main_response_count', 'main_flow_choice', '', 'MainIndex.value == MainResponseExpected.value'),
    edge('main_response_count', 'main_wait_actuator', '', 'else'),
    edge('main_flow_choice', 'main_send_response_start', '', 'MainFlow.value == 1'),
    edge('main_flow_choice', 'main_environment_status', '', 'MainFlow.value == 2'),
    edge('main_flow_choice', 'main_state_final', '', 'else'),
    edge('main_send_response_start', 'main_send_response_byte', 'MainStep.isCalled'),
    edge('main_send_response_byte', 'main_send_response_choice', 'MainStep.isCalled'),
    edge('main_send_response_choice', 'main_idle', '', 'MainIndex.value == MainResponseExpected.value'),
    edge('main_send_response_choice', 'main_send_response_byte', '', 'else'),
    edge('main_invalid', 'main_idle', 'MainStep.isCalled'),
    edge(
      'main_wait_sensors',
      'main_sensor_first',
      'MainBus.packetReceived',
      '',
      'MainSensors.set(MainSensorIndex.value, MainBus.lastData);\nMainSensorIndex.add(1);\nMainTimeout.start(2000);'
    ),
    edge('main_sensor_first', 'main_sensor_status', '', 'MainSensorIndex.value == 1'),
    edge('main_sensor_first', 'main_sensor_count', '', 'else'),
    edge('main_sensor_status', 'main_idle', '', 'MainBus.lastData == 224'),
    edge('main_sensor_status', 'main_idle', '', 'MainBus.lastData == 225'),
    edge('main_sensor_status', 'main_idle', '', 'MainBus.lastData == 226'),
    edge('main_sensor_status', 'main_idle', '', 'MainBus.lastData == 227'),
    edge('main_sensor_status', 'main_sensor_count', '', 'else'),
    edge('main_sensor_count', 'main_sensor_final', '', 'MainSensorIndex.value == 10'),
    edge('main_sensor_count', 'main_wait_sensors', '', 'else'),
    edge('main_sensor_final', 'main_calculate_door', '', 'MainBus.lastData == 3'),
    edge('main_sensor_final', 'main_idle', '', 'else'),
    edge('main_calculate_door', 'main_door_choice', 'MainStep.isCalled'),
    edge('main_door_choice', 'main_door_closed', '', 'MainDoorSum.value == 3'),
    edge('main_door_choice', 'main_door_open', '', 'else'),
    edge('main_door_closed', 'main_send_environment', 'MainStep.isCalled'),
    edge('main_door_open', 'main_send_environment', 'MainStep.isCalled'),
    edge('main_send_environment', 'main_wait_actuator', 'MainStep.isCalled'),
    edge('main_environment_status', 'main_get_state', '', 'MainBus.lastData == 3'),
    edge('main_environment_status', 'main_idle', '', 'else'),
    edge('main_get_state', 'main_wait_actuator', 'MainStep.isCalled'),
    edge('main_state_final', 'main_prepare_telemetry', '', 'MainBus.lastData == 3'),
    edge('main_state_final', 'main_idle', '', 'else'),
    edge('main_prepare_telemetry', 'main_send_telemetry_byte', 'MainStep.isCalled'),
    edge('main_send_telemetry_byte', 'main_telemetry_choice', 'MainStep.isCalled'),
    edge('main_telemetry_choice', 'main_end_telemetry', '', 'MainIndex.value == 29'),
    edge('main_telemetry_choice', 'main_send_telemetry_byte', '', 'else'),
    edge('main_end_telemetry', 'main_idle', 'MainStep.isCalled'),
    edge('main_control_command', 'main_idle', 'MainTimeout.timeout'),
    edge('main_receive_payload', 'main_idle', 'MainTimeout.timeout'),
    edge('main_wait_actuator', 'main_idle', 'MainTimeout.timeout'),
    edge('main_wait_sensors', 'main_idle', 'MainTimeout.timeout'),
  ];
  return graph('MS1MainProtocol', 'Центральная плата — маршрутизатор протокола', 'tjc-ms1-main-a4', nodes, edges);
}

function telemetryActions() {
  const lines = ['MainTelemetry.set(0, 1);'];
  const sensorMap = [
    [1, 0],
    [2, 4],
    [3, 5],
    [4, 6],
    [5, 7],
    [6, 8],
  ];
  for (const [target, source] of sensorMap) {
    lines.push(`MainSensors.peek(${source});`, `MainTelemetry.set(${target}, MainSensors.value);`);
  }
  lines.push('MainResponse.peek(0);', 'MainTelemetry.set(7, MainResponse.value);');
  lines.push('MainTelemetry.set(8, MainDoorOpen.value);');
  lines.push('MainTelemetry.set(9, MainDoorStage.value);');
  for (let source = 1; source <= 19; source += 1) {
    lines.push(
      `MainResponse.peek(${source});`,
      `MainTelemetry.set(${source + 9}, MainResponse.value);`
    );
  }
  lines.push('MainBus.sendPacket(99, 12);', 'MainIndex.reset();', 'MainStep.call();');
  return lines.join('\n');
}

function auxiliaryArduinoMachine() {
  const nodes = [
    state('aux_wait', 'Ждём', 'Button1.clicked/\nLED1.toggle();', 80, 20),
    vertex('aux_initial', 'initial', 0, 40),
    component('LED1', 'LED', { pin: 8 }),
    component('Button1', 'Button', { pin: 12, buttonMode: 'BUTTON_PULLDOWN' }),
  ];
  return graph('ArduinoUno1', 'Вспомогательная Arduino', 'ArduinoUno', nodes, [edge('aux_initial', 'aux_wait')]);
}

const document = `<?xml version="1.0" encoding="UTF-8"?>
<graphml xmlns="http://graphml.graphdrawing.org/xmlns">
  <data key="gFormat">Cyberiada-GraphML-1.0</data>
  <key attr.name="name" attr.type="string" for="node" id="dName"></key>
  <key attr.name="data" attr.type="string" for="node" id="dData"></key>
  <key attr.name="data" attr.type="string" for="edge" id="dData"></key>
  <key attr.name="initial" attr.type="string" for="node" id="dInitial"></key>
  <key for="edge" id="dGeometry"></key>
  <key for="node" id="dGeometry"></key>
  <key for="edge" id="dColor"></key>
  <key for="node" id="dNote"></key>
  <key for="node" id="dColor"></key>
  <key for="node" id="dVertex"></key>
  <key for="node" id="dStateMachine"></key>
  <key for="edge" id="dLabelGeometry"></key>
${indent(auxiliaryArduinoMachine(), 2)}
${indent(sensorMachine(), 2)}
${indent(centralMachine(), 2)}
${indent(actuatorMachine(), 2)}
</graphml>
`;

fs.writeFileSync(path.join(root, 'Скетч.graphml'), document);
console.log(`Generated Скетч.graphml (${document.length} bytes)`);
