import re
from graphviz import Digraph

class FunctionNode:
    def __init__(self, name, osd_parents):
        self.name = name
        self.children = []

        # in order to determine if a given node is a call to
        # scsi interface without any osd call in its call
        # trace, we track the number of osd calls in the call
        # trace.
        self.osd_parents = osd_parents

def parse_function_trace(input_text):
    lines = input_text.strip().split('\n')
    root = FunctionNode('start', 0)  # A dummy root to handle the overall hierarchy
    stack = [root]

    in_osd = 0

    for line in lines:
        # Check if the line defines a function block
        block_match = re.match(r'\s*(\w+)(.*)\(\)\s*{', line)
        # Check if the line represents a standalone function call
        call_match = re.match(r'\s*(\w+)(.*)\(\);', line)
        
        function_name = None
        new_node = None

        if block_match:
            function_name = block_match.group(1)
            new_node = FunctionNode(function_name, in_osd)
            if len(stack) != 0:
                stack[-1].children.append(new_node)
            stack.append(new_node)
        elif call_match:
            function_name = call_match.group(1)
            new_node = FunctionNode(function_name, in_osd)
            if len(stack) != 0:
                stack[-1].children.append(new_node)
            else:
                stack.append(new_node)
        elif len(stack) > 1 and '}' in line:  # End of a function block
            if 'osd' in stack[-1].name:
                in_osd -= 1
            stack.pop()

        if function_name and 'osd' in function_name:
            in_osd += 1
    
    return root

def visualize_function_trace(root):
    dot = Digraph()
    added_nodes = set()
    added_edges = set()

    def add_nodes_edges(node):
        if 'scsi' in node.name:
            if node.osd_parents == 0:
                # found a direct call to scsi function without
                # call to osd API in the call trace.
                # so, let's paint it red.
                dot.node(node.name, color='#ff1a1a', style='filled')
            else:
                dot.node(node.name, color='#ffd9b3', style='filled')
        elif 'osd' in node.name:
            dot.node(node.name, color='#b3e6ff', style='filled')
        elif 'exofs' in node.name:
            dot.node(node.name, color='#c6ffb3', style='filled')
        elif 'nvme' in node.name:
            dot.node(node.name, color="#fd054f6c", style='filled')
        else:
            dot.node(node.name)
        added_nodes.add(node.name)
        
        for child in node.children:
            if 'raw_spin' in child.name or 'cond_resched' in child.name or 'io_schedule' == child.name or \
            'kfree' == child.name or 'kmalloc' == child.name:
                continue
            edge = (node.name, child.name)
            if edge not in added_edges:
                dot.edge(node.name, child.name)
                added_edges.add(edge)
            add_nodes_edges(child)
    
    add_nodes_edges(root)
    return dot

def main():
    # for each ftrace output, let's parse it into a tree
    # and visualize it using graphviz.
    for filename in ['read.txt', 'write.txt', 'open.txt', 'close.txt', 'mount.txt']:
        with open(filename,"r") as input_file:
            input_text = input_file.read()
            
            # Parse the input text
            root_node = parse_function_trace(input_text)

            # Visualize the function trace
            dot = visualize_function_trace(root_node)
            dot.render(filename.split('.')[0], format='pdf', view=False)

            print(f"Done with {filename}.")


if __name__ == '__main__':
    main()
