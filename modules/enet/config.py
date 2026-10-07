def can_build(env, platform):
    return True


def configure(env):
    # wgodot-changes::begin
    # Public ENet classes are also included by native game modules.
    if env["builtin_enet"]:
        env.AppendUnique(wgodot_public_includes=["#thirdparty/enet/"])
    # wgodot-changes::end


def get_doc_classes():
    return [
        "ENetMultiplayerPeer",
        "ENetConnection",
        # wgodot-changes::begin
        "ENetConnectionEvent",
        # wgodot-changes::end
        "ENetPacketPeer",
    ]


def get_doc_path():
    return "doc_classes"
