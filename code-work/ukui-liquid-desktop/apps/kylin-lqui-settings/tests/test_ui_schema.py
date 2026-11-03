import json
from pathlib import Path
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from ui_schema import normalize_page, structural_signature, searchable_text, stable_key, field_visible


def field(fid, **values):
    return {**dict(id=fid, name='field' + fid, kind='text', label='字段' + fid, value=''), **values}


class Schema(unittest.TestCase):
    def page(self, fields, **ui):
        return {'fields': fields, 'ui': dict(version=1, groups=[{'key': 'a', 'title': '分组', 'order': 1}], **ui)}

    def test_old_protocol_and_unknown_version_preserve_order_and_visibility(self):
        raw = [field('2', ui={'group': 'a', 'order': -1, 'visibleWhen': {'name': 'field1', 'equals': False}}), field('1')]
        for ui in (None, {}, {'version': 2}, {'version': True}):
            page = normalize_page({'fields': raw, 'ui': ui})
            self.assertFalse(page['metadata']);self.assertEqual([f['id'] for f in page['fields']], ['2', '1'])
            self.assertTrue(all(not f['_ui'] for f in page['fields']))
            self.assertTrue(field_visible(page['fields'][0], {'field1': True}))

    def test_explicit_groups_order_and_description_ownership(self):
        data = self.page([field('1', ui={'group': 'a', 'order': 20}),
                          field('2', kind='note', ui={'group': 'a', 'role': 'help', 'for': 'field1'}),
                          field('3', ui={'group': 'a', 'order': 10})])
        page = normalize_page(data)
        self.assertEqual([r['fields'][0]['id'] for r in page['groups'][0]['rows']], ['3', '1'])
        self.assertEqual(page['groups'][0]['rows'][1]['attachments'][0]['id'], '2')

    def test_duplicate_group_disables_whole_metadata(self):
        data = self.page([field('1', ui={'group': 'a'})]);data['ui']['groups'] *= 2
        page = normalize_page(data);self.assertFalse(page['metadata']);self.assertEqual(page['fields'][0]['_ui'], {})

    def test_invalid_owner_only_falls_back_the_single_description(self):
        data = self.page([field('1', ui={'group': 'a'}),
                          field('2', kind='note', ui={'group': 'a', 'role': 'detail', 'for': 'missing', 'collapsible': True})])
        page = normalize_page(data);self.assertTrue(page['metadata'])
        self.assertFalse(page['fields'][1]['_ui']['collapsible']);self.assertNotIn('_absorbed', page['fields'][1])
        self.assertEqual(len(page['groups'][0]['rows']), 2)

    def test_duplicate_or_empty_names_are_instance_scoped(self):
        fields = [field('1'), field('2')];fields[1]['name'] = fields[0]['name']
        for instance in (1, 2):
            page = normalize_page({'fields': fields}, 'ai', 'general', instance)
            self.assertFalse(page['fields'][0]['_stable'])
            self.assertIn('@instance:' + str(instance), page['fields'][0]['_key'])
        self.assertNotEqual(stable_key('a', 'p', {'id': 'x'}, [], 1), stable_key('a', 'p', {'id': 'x'}, [], 2))

    def test_secret_values_are_not_cached_searched_or_diagnosed(self):
        item = field('1', secret=True);item['value'] = 'secret-do-not-export'
        page = normalize_page(self.page([item]));self.assertEqual(page['fields'][0]['value'], '')
        self.assertNotIn('secret-do-not-export', searchable_text(page['fields'][0]))
        self.assertNotIn('secret-do-not-export', json.dumps(page['diagnostics']))

    def test_content_updates_do_not_change_structure(self):
        data = self.page([field('1', kind='preview', ui={'group': 'a'})]);a = structural_signature(normalize_page(data))
        data['fields'][0].update(value='new png', previewStatus='ready', enabled=False)
        self.assertEqual(a, structural_signature(normalize_page(data)))
        data['fields'][0]['ui']['row'] = 'new-row'
        self.assertNotEqual(a, structural_signature(normalize_page(data)))

    def test_unknown_kind_and_component_are_retained(self):
        page = normalize_page(self.page([field('1', kind='future-kind', ui={'component': 'execute-script', 'group': 'a'})]))
        self.assertEqual(len(page['fields']), 1);self.assertTrue(page['fields'][0]['_unsupported'])
        self.assertNotIn('component', page['fields'][0]['_ui'])

    def test_entity_groups_interleaved_plugin_fields(self):
        data = self.page([field('1', ui={'group': 'a', 'entity': 'plugin-a', 'role': 'label'}),
                          field('2', ui={'group': 'a', 'entity': 'plugin-b'}),
                          field('3', ui={'group': 'a', 'entity': 'plugin-a', 'role': 'detail'})])
        rows = normalize_page(data)['groups'][0]['rows']
        self.assertEqual([[f['id'] for f in row['fields']] for row in rows], [['1', '3'], ['2']])


if __name__ == '__main__':
    unittest.main()
