package de.simon.dankelmann.submarine.ui.SignalGenerator.TabFragments.BinaryTab

import androidx.lifecycle.MutableLiveData
import androidx.lifecycle.ViewModel
import de.simon.dankelmann.submarine.Entities.SignalEntity
import de.simon.dankelmann.submarine.Models.SignalGeneratorDataModel

class BinaryTabViewModel : ViewModel() {
    var signalGeneratorDataModel = MutableLiveData<SignalGeneratorDataModel>().apply {
        value = SignalGeneratorDataModel()
    }
}