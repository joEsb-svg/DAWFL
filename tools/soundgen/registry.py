"""Elenco dei suoni: ogni funzione decorata con @sound diventa una voce della libreria."""

SOUNDS = []


def sound(sid, name, group, category, root=60, gated=False, release=0.15, colour=None):
    """
    sid       identificativo (nome del file, mai da cambiare: e' salvato nei progetti)
    name      nome mostrato (italiano)
    group     "Strumenti" o "Elettronica"
    category  sottocartella (es. "Pianoforti e tastiere", "Techno")
    root      nota MIDI a cui e' registrato il suono (le note del piano roll sono relative a questa)
    gated     True = nel piano roll la nota si ferma alla fine della sua durata (archi, organo, pad...)
    release   sfumatura in secondi alla fine della nota (solo se gated)
    """
    def deco(fn):
        SOUNDS.append(dict(id=sid, name=name, group=group, category=category, root=root,
                           gated=gated, release=release, colour=colour, fn=fn))
        return fn
    return deco
