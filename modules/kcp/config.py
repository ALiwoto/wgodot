# wgodot-changes::file
def can_build(env, platform):
    return platform != "web"


def configure(env):
    pass


def get_doc_classes():
    return ["PacketPeerKCP"]


def get_doc_path():
    return "doc_classes"
