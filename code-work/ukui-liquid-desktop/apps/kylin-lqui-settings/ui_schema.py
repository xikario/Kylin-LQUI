"""Optional presentation metadata. Native runtime IDs remain the write protocol."""
from collections import Counter
from copy import deepcopy
import json
import math

ROLES = {'control', 'label', 'help', 'detail', 'warning', 'status', 'heading', 'preview', 'action'}
COMPONENTS = {'auto', 'file-picker', 'preview', 'plugin-row'}
KINDS = {'boolean', 'number', 'text', 'multiline', 'choice', 'tabs', 'list', 'action', 'toggle', 'note', 'heading', 'preview'}


def stable_key(module, page, field, fields, instance=0):
    name = field.get('name')
    if isinstance(name, str) and name and sum(f.get('name') == name for f in fields) == 1:
        ui = field.get('_ui', field.get('ui', {}))
        return (module, page, name, ui.get('entity', '') if isinstance(ui, dict) else '')
    return (module, page, '@instance:' + str(instance), str(field.get('id', '')))


def validate_ui_metadata(data):
    """Page errors disable all metadata; field errors affect only that field.

    Diagnostics contain fixed codes and indices, never values/labels/paths.
    """
    errors = []
    ui = data.get('ui')
    if not isinstance(ui, dict) or type(ui.get('version')) is not int or ui['version'] != 1:
        return False, [], [{'code': 'legacy-or-unsupported-version'}]
    groups = ui.get('groups', [])
    if not isinstance(groups, list):
        return False, [], [{'code': 'invalid-groups'}]
    keys = set()
    for group in groups:
        if (not isinstance(group, dict) or not isinstance(group.get('key'), str) or not group['key']
                or group['key'] in keys or not isinstance(group.get('title', ''), str)
                or type(group.get('order', 0)) is not int or type(group.get('columns', 1)) is not int
                or group.get('columns', 1) not in (1, 2)):
            return False, [], [{'code': 'invalid-or-duplicate-group'}]
        keys.add(group['key'])
    return True, groups, errors


def _field_ui(field, names, groups, index, errors):
    ui = field.get('ui', {})
    if not isinstance(ui, dict):
        errors.append({'code': 'invalid-field-ui', 'index': index})
        return {}
    result = {}
    for key in ('group', 'row', 'role', 'for', 'component', 'emphasis', 'entity', 'alt'):
        if key in ui:
            if not isinstance(ui[key], str):
                errors.append({'code': 'invalid-field-type', 'index': index})
                return {}
            result[key] = ui[key]
    if 'order' in ui:
        if type(ui['order']) is not int:
            errors.append({'code': 'invalid-order', 'index': index})
            return {}
        result['order'] = ui['order']
    if 'collapsible' in ui:
        if type(ui['collapsible']) is not bool:
            errors.append({'code': 'invalid-collapsible', 'index': index})
            return {}
        result['collapsible'] = ui['collapsible']
    if result.get('component', 'auto') not in COMPONENTS:
        result.pop('component', None)
    if result.get('role', 'control') not in ROLES:
        result.pop('role', None)
    if result.get('emphasis', 'normal') not in ('normal', 'primary', 'destructive'):
        result.pop('emphasis', None)
    if result.get('group') and result['group'] not in groups:
        errors.append({'code': 'missing-group', 'index': index})
        result.pop('group')
    owner = result.get('for')
    if owner:
        if names[owner] != 1:
            errors.append({'code': 'missing-or-ambiguous-owner', 'index': index})
            result.pop('for')
            result['collapsible'] = False
    condition = ui.get('visibleWhen')
    if condition is not None:
        if (isinstance(condition, dict) and isinstance(condition.get('name'), str)
                and names[condition['name']] == 1 and 'equals' in condition
                and type(condition['equals']) in (bool, int, str, float, type(None))
                and (type(condition['equals']) is not float or math.isfinite(condition['equals']))):
            result['visibleWhen'] = {'name': condition['name'], 'equals': condition['equals']}
        else:
            errors.append({'code': 'invalid-visible-condition', 'index': index})
    return result


def normalize_page(data, module='', page='', instance=0):
    valid, definitions, errors = validate_ui_metadata(data)
    fields = deepcopy(data.get('fields', []))
    names = Counter(f.get('name') for f in fields if isinstance(f.get('name'), str) and f.get('name'))
    known_groups = {g['key'] for g in definitions}
    for index, field in enumerate(fields):
        if not isinstance(field.get('name',''),str):field['name']=''
        if not isinstance(field.get('kind'),str):field['kind']='unsupported'
        field['_ui'] = _field_ui(field, names, known_groups, index, errors) if valid else {}
        field['_index'] = index
        field['_stable'] = isinstance(field.get('name'), str) and bool(field['name']) and names[field['name']] == 1
        field['_key'] = stable_key(module, page, field, fields, instance)
        field['_role'] = field['_ui'].get('role') or {
            'note': 'help', 'heading': 'heading', 'preview': 'preview', 'action': 'action'
        }.get(field.get('kind'), 'control')
        if field.get('secret'):
            field['value'] = ''
            field['_ui'].pop('alt', None)
        field['_unsupported'] = field.get('kind') not in KINDS
    normalized = {'fields': fields, 'metadata': valid, 'diagnostics': errors,
                  'layout': data.get('ui', {}).get('layout', 'grouped') if valid else 'grouped',
                  'definitions': definitions if valid else []}
    if normalized['layout'] not in ('grouped', 'plugin-list'):
        normalized['layout'] = 'grouped'
    normalized['groups'] = build_groups(normalized)
    return normalized


def build_groups(page):
    groups = [{**g, 'rows': [], 'sourceHeadings': []} for g in page['definitions']]
    by_group = {g['key']: g for g in groups}
    by_name = {f['name']: f for f in page['fields'] if f.get('_stable')}
    attachments = {}
    for field in page['fields']:
        owner = field['_ui'].get('for')
        if owner:
            target=by_name.get(owner);target_ui=target['_ui'] if target else {}
            ordinary=target and target['kind'] in {'boolean','number','text','multiline','choice','tabs','list','toggle'} and not target.get('secret') and not target_ui.get('entity') and not str(target.get('name','')).startswith('liquidMaterialPreset_')
            shared=target_ui.get('row') and any(other is not target and other['_ui'].get('row')==target_ui['row'] and other['_ui'].get('group')==target_ui.get('group') and not other['_ui'].get('for') for other in page['fields'])
            if not ordinary or shared or field['kind'] not in {'note','heading'} or field['_role'] not in {'label','help','detail'}:
                field['_ui'].pop('for',None);field['_ui']['collapsible']=False;page['diagnostics'].append({'code':'unsupported-description-owner','index':field['_index']});continue
            attachments.setdefault(owner, []).append(field)
            field['_absorbed'] = 'associated-description'
    legacy_group = None
    row_maps = {}
    for field in page['fields']:
        if field.get('_absorbed'):
            continue
        ui = field['_ui']
        if field.get('name') == 'pageHeading' and field.get('kind') == 'heading':
            field['_absorbed'] = 'page-title'
            continue
        group = by_group.get(ui.get('group'))
        if group is not None and field['_role'] == 'heading' and not ui.get('entity'):
            group['sourceHeadings'].append(field)
            field['_absorbed'] = 'group-title'
            continue
        if group is None:
            if legacy_group is None or (field.get('kind') == 'heading' and not ui):
                legacy_group = {'key': '@legacy:' + str(field['_index']),
                                'title': str(field.get('value', '')) if field.get('kind') == 'heading' else '',
                                'order': 100000 + field['_index'], 'columns': 1, 'rows': [], 'sourceHeadings': []}
                groups.append(legacy_group)
                if field.get('kind') == 'heading':
                    field['_absorbed'] = 'group-title'
                    legacy_group['sourceHeadings'].append(field)
                    continue
            group = legacy_group
        row_key = ui.get('entity') or ui.get('row')
        mapping = row_maps.setdefault(group['key'], {})
        if row_key and row_key in mapping:
            row = mapping[row_key]
        elif (field.get('kind') == 'action' or field.get('kind') == 'toggle' and str(field.get('name','')).startswith('liquidMaterialPreset_')) and group['rows'] and group['rows'][-1].get('actions') and not row_key:
            row = group['rows'][-1]
        else:
            row = {'key': row_key or field['_key'], 'fields': [], 'attachments': [],
                   'order': ui.get('order', field['_index']), 'actions': field.get('kind') == 'action' or field.get('kind') == 'toggle' and str(field.get('name','')).startswith('liquidMaterialPreset_'),
                   'entity': ui.get('entity', '')}
            group['rows'].append(row)
            if row_key:
                mapping[row_key] = row
        row['fields'].append(field)
        row['attachments'].extend(attachments.get(field.get('name'), []))
    if page['metadata']:
        groups.sort(key=lambda g: g.get('order', 0))
        for group in groups:
            group['rows'].sort(key=lambda row: row['order'])
    return groups


def presentation_rows(row):
    """Unknown row combinations fall back to one visible field per row."""
    fields=row['fields'];kinds=[f['kind'] for f in fields]
    if row.get('entity'):
        plugin=kinds.count('heading')==1 and kinds.count('boolean')<=1 and all(f['kind'] in {'heading','boolean'} or f['kind']=='note' and (f['_role']=='detail' or str(f.get('name','')).startswith(('state:','unknown:'))) for f in fields)
        if plugin:return [row]
    else:
        if len(fields)==1 or all(kind in {'note','heading'} for kind in kinds):return [row]
        if row['actions'] and all(kind in {'action','toggle'} for kind in kinds):return [row]
        if kinds.count('preview')==1 and all(f['kind'] in {'preview','note','action'} or f['kind']=='text' and f['_ui'].get('component')=='file-picker' for f in fields):return [row]
    result=[]
    for field in fields:
        result.append({**row,'key':(row['key'],field['_key']),'fields':[field],'entity':'',
                       'actions':field['kind']=='action' or field['kind']=='toggle' and str(field.get('name','')).startswith('liquidMaterialPreset_'),
                       'attachments':[note for note in row['attachments'] if note['_ui'].get('for')==field.get('name')]})
    return result


def field_visible(field, values):
    condition = field['_ui'].get('visibleWhen')
    if not condition:
        return True
    # JSON booleans and numbers are distinct; never evaluate expressions.
    value = values.get(condition['name'])
    wanted = condition['equals']
    return type(value) is type(wanted) and value == wanted


def searchable_text(field):
    text = [field.get('label', ''), field.get('help', '')]
    if not field.get('secret'):
        kind=field.get('kind');value=field.get('value')
        if kind=='preview':text.extend([field.get('previewStatus',''),field.get('_ui',{}).get('alt','')])
        elif kind=='choice' and field.get('editable') and 'text' in field:text.append(field['text'])
        elif kind in ('choice','tabs') and type(value) is int and 0<=value<len(field.get('options',[])):text.append(field['options'][value])
        elif kind=='list':text.extend(item.get('text','') for item in field.get('items',[]))
        elif type(value) is bool:text.append('开启' if value else '关闭')
        else:text.append(str(value or ''))
    return ' '.join(str(t) for t in text).casefold()


def structural_signature(page):
    fields = []
    for field in page['fields']:
        fields.append({k: v for k, v in field.items() if not k.startswith('_')
                       and k not in ('value', 'help', 'enabled', 'text', 'previewStatus', 'items', 'ui')})
        fields[-1]['ui'] = field['_ui']
    return json.dumps({'fields': fields, 'groups': page['definitions'], 'layout': page['layout']},
                      ensure_ascii=False, sort_keys=True)
