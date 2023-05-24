#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2008-2023 HPDCS Group <rootsim@googlegroups.com>
# SPDX-License-Identifier: GPL-3.0-only

import json
import random
import xml.etree.ElementTree as Et

import osmnx as ox

# Bounding boxes: (north, south, east, west)
# Get the coordinates from https://www.openstreetmap.org/export
BBOXES = {
    "aurelio": (41.9261, 41.8724, 12.4957, 12.3681),
    "rome": (41.9927, 41.7908, 12.6195, 12.3703),
    "shenzhen": (22.8603, 22.506, 114.4363, 113.7291),
    "rostock": (54.2592, 54.0533, 12.2106, 11.9909),
    "trento": (46.1541, 45.9798, 11.1676, 11.0200)
}

ENTER_FR_MIN = 10
ENTER_FR_MAX = 30
LEAVE_PROB_MIN = 0.1
LEAVE_PROB_MAX = 0.5

# GraphML namespace
NS = {'graphml': 'http://graphml.graphdrawing.org/xmlns'}


def get_length_attribute(root):
    for key in root.findall('graphml:key', namespaces=NS):
        if key.attrib['attr.name'] == 'length':
            return key.attrib['id']
    return None


def get_all_nodes(root):
    nodes = root.findall('graphml:graph/graphml:node', namespaces=NS)
    id_list = [node.attrib['id'] for node in nodes]

    node_dict = {
        node_id: {'index': index,
                  'enter_freq': random.uniform(ENTER_FR_MIN, ENTER_FR_MAX),
                  'leave_prob': random.uniform(LEAVE_PROB_MIN, LEAVE_PROB_MAX)}
        for index, node_id in enumerate(id_list)
    }

    return node_dict


def get_all_edges(root, nodes, length_attribute):
    edges = root.findall('graphml:graph/graphml:edge', namespaces=NS)
    start_at = len(nodes)
    edge_dict = {index + start_at: {'source': nodes[edge.attrib['source']]['index'],
                                    'target': nodes[edge.attrib['target']]['index'],
                                    'length': edge.find('graphml:data[@key="' + length_attribute + '"]',
                                                        namespaces=NS).text}
                 for index, edge in enumerate(edges)}
    return edge_dict


def sanitize_nodes(nodes):
    transformed_dict = {nodes[key]['index']: {'enter_freq': nodes[key]['enter_freq'],
                                              'leave_prob': nodes[key]['leave_prob']}
                        for key in nodes}
    return transformed_dict


def download_osm_data(city):
    bbox = BBOXES[city]
    graph = ox.graph_from_bbox(bbox[0], bbox[1], bbox[2], bbox[3], clean_periphery=True, network_type='drive',
                               retain_all=False)
    fig, ax = ox.plot_graph(graph)
    fig.savefig(f"{city}.png")
    ox.save_graphml(graph, filepath=f"{city}.gml", gephi=True, encoding='utf-8')


def main(city):
    download_osm_data(city)

    tree = Et.parse(f"{city}.gml")
    root = tree.getroot()
    length_attribute = get_length_attribute(root)
    nodes = get_all_nodes(root)
    edges = get_all_edges(root, nodes, length_attribute)

    config = {'num_nodes': len(nodes),
              'num_edges': len(edges),
              'nodes': sanitize_nodes(nodes),
              'edges': edges}

    json_string = json.dumps(config, indent=2)

    with open(f"{city}.json", "w") as file:
        file.write(json_string)


if __name__ == "__main__":
    main("shenzhen")
