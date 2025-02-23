#include "RE/BGSDistantTreeBlock.h"
#include "RE/BSFadeNode.h"
#include "RE/NiNode.h"
#include "RE/TESObjectREFR.h"

#include "skse64/GameSettings.h"

#include "tbb/concurrent_hash_map.h"

#include "patches.h"
#include "RE/TESDataHandler.h"
#include "RE/TESObjectSTAT.h"


namespace patches
{
    typedef void(*UpdateBlockVisibility_)(RE::BGSDistantTreeBlock::ResourceData * data);
    RelocAddr<UpdateBlockVisibility_> UpdateBlockVisibility_orig(UpdateBlockVisibility_orig_offset);

    typedef uint16_t(*Float2Half_)(float f);
    RelocAddr<Float2Half_> Float2Half(Float2Half_offset);

    typedef RE::TESForm* (*_LookupFormByID)(uint32_t id);
    RelocAddr<_LookupFormByID> LookupFormByID(LookupFormByID_offset);

    tbb::concurrent_hash_map<uint32_t, RE::TESObjectREFR *> referencesFormCache;

    void InvalidateCachedForm(uint32_t FormId)
    {
        referencesFormCache.erase(FormId & 0x00FFFFFF);
    }

    void hk_UpdateBlockVisibility(RE::BGSDistantTreeBlock::ResourceData *data)
    {
        for (uint32_t i = 0; i < data->lodGroups.size(); i++)
        {
            auto *group = data->lodGroups[i];

            for (uint32_t j = 0; j < group->lodInstances.size(); j++)
            {
                RE::BGSDistantTreeBlock::LODGroupInstance *instance = &group->lodInstances[j];
                const uint32_t maskedFormId = instance->formId & 0x00FFFFFF;

                RE::TESObjectREFR *refrObject = nullptr;

                tbb::concurrent_hash_map<uint32_t, RE::TESObjectREFR *>::accessor accessor;

                if (referencesFormCache.find(accessor, maskedFormId))
                {
                    refrObject = accessor->second;
                }
                else
                {
                    // Find first valid tree object by ESP/ESM load order
                    for (int k = 0; k < RE::TESDataHandler::GetSingleton()->modList.loadedMods.size(); k++)
                    {
                        RE::TESForm *form = LookupFormByID((k << 24) | maskedFormId);
                        if (form)
                            refrObject = form->GetReference();
                        if (refrObject)
                        {
                            RE::TESForm * baseForm = refrObject->baseForm;
                            if (baseForm)
                            {
                                // Checks if the form type is TREE (TESObjectTREE) or if it has the kHasTreeLOD flag (TESObjectSTAT)
                                if (baseForm->flags & RE::TESObjectSTAT::RecordFlags::kHasTreeLOD || baseForm->formType == RE::FormType::Tree)
                                    break;
                            }
                        }

                        refrObject = nullptr;
                    }

                    // Insert even if it's a null pointer
                    referencesFormCache.insert(std::make_pair(maskedFormId, refrObject));
                }

                bool fullyHidden = false;
                float alpha = 1.0f;

                if (refrObject)
                {
                    RE::NiNode * node = refrObject->GetNiNode();
                    RE::TESObjectCELL * cell = refrObject->GetParentCell();
                    // NiNode::GetAppCulled, TESObjectCELL::IsAttached
                    if (node && !(*(BYTE *)((__int64)node + 0xF4) & 1) && *(uint8_t *)((__int64)cell + 0x44) == 7)
                    {
                        if (GetINISetting("bEnableStippleFade:Display")->data.u8 >= 1)
                        {
                            const auto fadeNode = node->GetAsBSFadeNode();
                            if (fadeNode)
                            {
                                alpha = 1.0f - fadeNode->currentFade;
                                if (alpha <= 0.0f)
                                    fullyHidden = true;
                            }

                        }
                        else
                        {
                            // No alpha fade - LOD trees will instantly appear or disappear
                            fullyHidden = true;
                        }
                    }

                    if (refrObject->flags & RE::TESObjectREFR::RecordFlags::kInitiallyDisabled || refrObject->flags & RE::TESObjectREFR::RecordFlags::kDeleted)
                        fullyHidden = true;
                }

                const uint16_t halfFloat = Float2Half(alpha);

                if (instance->alpha != halfFloat)
                {
                    instance->alpha = halfFloat;
                    group->unk24 = false;
                }

                if (instance->hidden != fullyHidden)
                {
                    instance->hidden = fullyHidden;
                    group->unk24 = false;
                }

                if (fullyHidden)
                    data->unk82 = false;
            }
        }
    }

    bool PatchTreeLODReferenceCaching()
    {
        _VMESSAGE("- Tree LOD Reference Caching -");

        _VMESSAGE("detouring UpdateLODAlphaFade");
        g_branchTrampoline.Write6Branch(UpdateBlockVisibility_orig.GetUIntPtr(), GetFnAddr(hk_UpdateBlockVisibility));
        _VMESSAGE("success");

        return true;
    }

}
